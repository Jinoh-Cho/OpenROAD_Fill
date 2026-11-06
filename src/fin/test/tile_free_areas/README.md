# Global versus tile-local free-area experiment

The production LP call now uses `makeLayerFillAreasFromRectangles` for
nonnegative metal spacing. It distributes already expanded source rectangles
to every intersecting tile, clips them there, then computes each tile's obstacle
union/complement in parallel. Both paths share the exact same tile spacing and
pruning function. Negative origins and clipped boundary tiles are supported.
Expanding before clipping preserves spacing contributions from outside each tile.

LP collection requests only original polygons plus expanded source rectangles;
it no longer builds a redundant global expanded polygon set. Other callers can
keep the polygon-only behavior. Negative spacing retains the global path. Degenerate
input fallback uses rectangles extracted from the legacy expanded union.

The tests compare each tile's geometry, blocked area, and exact ordered candidate
list. Cases include no obstacles, fully blocked regions, holes, overlap, boundary
crossings, outside obstacles, negative coordinates and both layer directions.

```bash
bazelisk test //src/fin/test/tile_free_areas:tile_free_areas_test --jobs=16 --test_output=errors
python3 src/fin/test/script/run_tile_free_area_benchmark.py /absolute/path/to/input.odb
```

The second command needs the built test binary. It measures M2 under the TPU
ASAP7 rule dimensions, with a 50um window and resolution4. It runs global and tiled
preparation at1T/16T, three trials each, alternating which method runs first.
The report includes distribution, subtraction/union, global decomposition, tile
spacing, and pruning. Timed preparation excludes loading, common collection,
reference copies, geometry comparisons, candidate comparisons, LP, and placement.
A fresh copy of raw global edges is supplied for each trial outside the timer to
avoid reusing a normalized polygon cache. Tile inputs are immutable rectangles.
The log/trial CSV/JSON/Markdown are saved to the printed results directory.

Do not interpret this as end-to-end fill speedup. Tile-local subtraction includes
normalizing local geometry and calculating covered areas; the global rectangle
decomposition step disappears. Distribution remains serial in this prototype.

Runtime logs report `free_area_mode=tile` (or global for negative spacing), and
CSV stores `candidate_free_area_tiled=1`. The input rectangle buffer is released
immediately after preparation; its elapsed cleanup is recorded as
`candidate_tile_input_cleanup_seconds`. Threads follow `candidate_threads`.
The helper benchmark retains the global path for comparison.

## Initial metal density

Initial LP density now uses the same tile partitioner on unexpanded source
rectangles collected during the existing OpenDB traversal. Each tile independently
unions overlapping metal and computes covered area, then the existing prefix-sum
window density calculation runs unchanged. Workers follow `candidate_threads`;
GUI/tracing force one worker. Original source buffers are released after density.
The polygon-based density function remains for reference and full verification.

```bash
python3 src/fin/test/script/run_tile_free_area_benchmark.py /absolute/path/to/input.odb --mode density
```

This runs1T/16T/32T tiled initial density against the serial legacy implementation,
three trials each. It checks exact tile/window areas and densities and preserves
candidate lists. Timers include partition, local union/area, worker buffer cleanup
and window density. Loading, common source collection, reference copies and
correctness checks are excluded. It is not an end-to-end fill benchmark.

Production logs report `initial_density_mode=tile` and actual
`initial_density_threads`. Initial density breakdown now includes
`initial_partition_seconds`, `initial_tile_union_area_seconds`,
`initial_density_cleanup_seconds` and `initial_input_cleanup_seconds`.
The old standalone global union/decompose counters are zero in this mode;
local union/decomposition is included in tile_union_area, not eliminated.
Collection and whole-fill time must also be compared after a large-design rebuild,
since the added source-rectangle stream has a collection/memory cost.

## Final candidate-size update optimization

`makeFillCandidates` skips the blocked-area bloat/accumulation and final
`fill_area` subtraction after the last configured size. Candidate enumeration,
intersection, filtering, and rectangle extraction still run for every size.
Earlier sizes retain the update so smaller/later sizes cannot reuse that space.
No candidate generation option or solver setting is changed.

`CandidateReference.cpp` is a test-only frozen copy of the previous enumeration
body (using the unchanged shared geometry primitives). It is not linked into
OpenROAD. Unit tests compare exact candidate vectors for single, multiple,
duplicate, ascending/descending, odd and unfillable final sizes; holes, disjoint
regions, negative coordinates and both routing directions are included.

```bash
python3 src/fin/test/script/run_tile_free_area_benchmark.py /absolute/path/to/input.odb --mode candidate
```

Use `--thread-counts 32` to measure only 32 threads (or supply several counts).
This selection applies to candidate generation; common collection and tile
preparation still use 16 threads and are outside the measured interval.

This compares old/new candidate generation on the same prepared TPU M2 tile
regions at1T/16T/32T, three trials each, alternating order. Each timed call gets
the same initial polygon state through a fresh copy outside the timer. The
parallel schedule is dynamic16 for both methods, matching production scheduling.
Loading, collection, tile preparation, reference copies and exact vector checks
are excluded. Total candidate count, coordinates and order must match in every
trial. Small-input timing does not establish a large-design or full-fill speedup.

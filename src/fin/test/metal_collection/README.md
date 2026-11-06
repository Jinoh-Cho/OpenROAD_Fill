# Parallel initial metal collection

`orNonFills` reads regular wires, special wires/vias, and instance geometry with
worker-local iterators and polygon sets. At most 65536 object pointers are queued
at once. Static contiguous partitions and raw edge concatenation preserve serial
insertion order. Collection does not modify OpenDB or normalize partial polygons.
Exceptions propagate after workers join. One thread uses a direct serial path.

LP fill uses `candidate_threads` workers for collection. For a controlled serial
baseline, pass `--serial-metal-collection` to either experiment script; this sets
`FIN_SERIAL_METAL_COLLECTION=1` and forces only collection to one thread.
Without the flag, normal candidate-thread behavior is retained. Graphics and
candidate tracing also force collection to one thread. Other callers stay serial.
Rebuild OpenROAD before running fill experiments with the change.

Logging and CSV report actual `metal_collect_threads`, the three collection
phases and their total. `initial_collect_enumerate_seconds` and
`initial_collect_merge_seconds` are diagnostics included in those phases;
do not add them to the total. Merge includes releasing worker edge buffers.
Initial density union/clip is unchanged and timed separately.

The synthetic test covers overlapping regular/via geometry, special wires and
block vias, rotated/mirrored instances with pins and obstructions, negative
coordinates, empty nets, batch boundaries, invalid thread counts and empty blocks.
It compares raw edge order and geometric differences at 1, 2 and 16 threads.

```bash
bazelisk test //src/fin/test/metal_collection:metal_collection_test --jobs=16 --test_output=errors
```

An optional integration test reads an existing small ODB and compares M2 raw
edges and geometry at 1 and 16 threads without modifying the input. Run the built
test binary with `FIN_METAL_TEST_ODB=/absolute/path/to/small.odb`. It reports
collection wall time separately from loading, reference copies and comparison.
This is a collection/geometry test; it does not run an LP fill or establish a
large-design speedup. CMake also registers MetalCollectionTest.

After rebuilding OpenROAD, compare only metal collection (10x10 ODB, all other
stages fixed at16T, SVG OFF, one repeat per method):

```bash
python3 src/fin/test/script/run_candidate_summary_sweep.py --size 10x10 --methods min_amount lp_minvar --repeats 1 --summary-thread-counts 16 --serial-metal-collection
python3 src/fin/test/script/run_candidate_summary_sweep.py --size 10x10 --methods min_amount lp_minvar --repeats 1 --summary-thread-counts 16
```

Each command creates its own results directory. The worker checks that a serial
baseline actually reports `metal_collect_threads=1`; an incompatible older binary
is marked `configuration_mismatch` rather than accepted as a serial result.

## Fused spacing expansion

LP fill now asks collection to produce both original rectangles and rectangles
expanded by nonnegative `space_to_non_fill`. Dilation distributes over union, so
full source rectangles can be expanded independently without halos or spatial
clipping. Both outputs use bounded worker-local buffers and ordered raw edge
concatenation. Original geometry still feeds initial density. The expanded rectangles
feed tile-local free-area subtraction directly; negative resizing retains the old path.

Zero-area source polygons can have nonempty artifacts under legacy Boost polygon
conversion. To preserve previously accepted results, that rare case retries the
old collection and Boost expansion for the layer. Overflow is checked explicitly.
The synthetic test checks this fallback, holes/overlaps, gap closure, zero spacing,
1/2/16 workers, vias, rotations and batch boundaries. The existing-ODB integration
test checks original edge order, symmetric geometric differences and the exact
ordered free-rectangle list against legacy expansion before LP uses the new path.

`spacing_fused=1` in the collection log and `metal_spacing_fused` in CSV mean
spacing was built directly. Collection phase times now include expansion and its
buffer merging. `candidate_bloat_seconds` is zero when spacing was already
prepared during collection, including the degenerate fallback. It does not mean
spacing work is free. The fallback also logs retry and legacy-expansion time.
Compare collection + spacing + free subtraction + free decomposition and full
fill time across builds; downstream union work can move into subtraction.
Original and expanded edge buffers coexist during initial density, so peak memory
on large inputs must be assessed after rebuilding and running the full flow.

LP now requests the expanded rectangle list without constructing a global expanded
polygon set. This is consumed by tile-local preparation and released before
candidate generation. Initial density now consumes an unexpanded source-rectangle stream from the
same traversal. Original polygons remain available for SVG and verification.

Source rectangles are clipped into every intersecting density tile. Zero-area
source geometry uses normalized legacy rectangles for both initial density and
spacing, preserving previously accepted geometry. The original raw polygon stream
is retained unchanged; no second OpenDB traversal is needed for normal input.

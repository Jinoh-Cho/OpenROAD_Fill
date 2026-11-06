# Replicate a routed TPU directly in OpenDB

`replicate_odb.py` launches the adjacent `replicate_odb.tcl` using the existing
Tcl-enabled OpenROAD executable. No OpenROAD source changes, C++ compilation,
Python odb package, or expanded DEF intermediate is required.

Run from `/home/jocho/OpenFill`. For a first small TPU test:

```bash
python3 OpenROAD_Fill/src/fin/test/script/replicate_odb.py asap7/tpu/tpu.orfs.out.def benchmarks/asap7_tpu_orfs/expanded_odb/tpu_orfs_2x2.odb --nx 2 --ny 2 --share-power
```

10x10:

```bash
python3 OpenROAD_Fill/src/fin/test/script/replicate_odb.py asap7/tpu/tpu.orfs.out.def benchmarks/asap7_tpu_orfs/expanded_odb/tpu_orfs_10x10.odb --nx 10 --ny 10 --share-power
```

50x50 (same final object counts and large memory requirement as the DEF design):

```bash
python3 OpenROAD_Fill/src/fin/test/script/replicate_odb.py asap7/tpu/tpu.orfs.out.def benchmarks/asap7_tpu_orfs/expanded_odb/tpu_orfs_50x50.odb --nx 50 --ny 50 --share-power
```

Defaults: `OpenROAD_Fill/bazel-bin/openroad` (or `OPENROAD_BIN`) and the two ASAP7
LEFs under `$ORFS_ROOT/flow/platforms/asap7/lef` (default
`~/OpenROAD-flow-scripts`). Override with `--openroad` and `--lefs TECH.lef CELLS.lef ...`.
An original single-block `.odb` may also be used as input; LEFs are then loaded
from the ODB. Use the same OpenROAD build to create and reload the database.
Do not assume ODB schema compatibility across versions.

## Output and behavior

- Read the original once, snapshot source objects and compile wire operations.
- Keep the original as tile (0,0), with original names. Other tiles get
  `tCOLUMN_ROW__` prefixes. Names/IDs differ from the text DEF replicator.
- Clone instance masters/origins/orientations/status, net types and connections,
  boundary pin boxes/status, regular routing including relative RECTs, branch
  junctions and vias, special routing, placement rows, tracks, and GCell patterns.
- Preserve block via definitions and NDR references in the same database.
- Update die area and recompute the cached core area from the expanded rows.
- With `--share-power`, POWER/GROUND special nets are shared; cloned ITerms are
  connected directly, without a repeated wildcard scan over the expanded design.
- Without `--share-power`, each copy has separate power nets. Original DEF
  wildcard connections have already been resolved by the initial `read_def`, so
  this mode also works on the TPU source. Clear wildcard flags before exporting
  an optional DEF so a subsequent read cannot accidentally reconnect copies.
- `--gap-x` and `--gap-y` use database units. Coordinate overflow is rejected.
- Print template progress and completed tiles. Replication is serial; `--threads`
  configures OpenROAD, not a parallel replication algorithm.
- Save `.odb` and `<output>.odb.json`, including phase times and derived object
  counts. Existing outputs are rejected. Database publication uses an atomic
  no-overwrite link after writing succeeds. Incomplete staging files are cleaned
  up on ordinary exit/interruption; SIGKILL can leave `.replicate_odb_*` folders.
- `--write-def PATH` optionally exports a DEF for small-case verification.

The supported scope is flat, unfilled routed benchmark blocks. Children,
regions/groups, placement/routing blockages, existing fills, masked signal wires,
masked pin/special-wire boxes, masked special vias, octilinear special wires, and
nonzero wire-point properties are rejected. It is not a complete clone of timing,
parasitic, antenna, or arbitrary custom-property metadata. These restrictions
avoid silently changing unsupported routing or floorplan constraints.

## Reload and fill

In the experiment Tcl, load the ODB with:

```tcl
read_db /home/jocho/OpenFill/benchmarks/asap7_tpu_orfs/expanded_odb/tpu_orfs_10x10.odb
# Existing fill commands follow. LEFs are already stored in the ODB.
```

The shared experiment worker now supports `.odb` via `read_db` and skips LEF
preloading. Pass `--input-format odb --input-dir PATH` to
`scripts/run_tpu_combined_thread_sweep.sh`; files must use the usual
`tpu_orfs_SIZE.odb` names. DEF mode remains the default. Existing DEF replicators
are unchanged.

## Verification

```bash
python3 OpenROAD_Fill/src/fin/test/script/test_replicate_odb.py
```

Integration tests use a small native database with branches, extensions, a via,
relative RECTs and terminal opcodes; check 2x2 shared/separate power, shifted
coordinates and core/die areas, DEF input, no-overwrite, overflow and unsupported
mask rejection. A 5,000-wire fixture exercises repeated decoder allocation and
destruction. Uses the existing OpenROAD binary.

Tcl-owned temporary objects must be destroyed using `$object -delete` so their
registered object commands and native storage are released together. Calling
`odb::delete_dbWireDecoder $object` alone leaves a stale owning command and can
cause a double free when the allocator reuses that address. The worker uses the
Tcl ownership-aware operation for decoders, the encoder, and its temporary Rect.

Actual TPU 2x1 comparisons and MIN VAR logs are recorded under
`runs/odb_replication_validation_20261105/` in the workspace. These small tests
do not establish a speedup or memory reduction for 20x20/50x50 databases.

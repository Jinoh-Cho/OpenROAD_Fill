# Native OpenDB replication

Separate implementation: `replicate_odb_native.py` launches the standalone C++
helper in `odb_native/replicate_odb.cpp`. Existing `replicate_odb.py/.tcl` and DEF
replicators are unchanged. The loops that create instances, connect terminals,
and translate/re-encode routing run entirely in C++, without Tcl/SWIG calls.

## Build and run

Run from `/home/jocho/OpenFill`. Build only the helper (not all of OpenROAD) and
create a small 2x2 TPU:

```bash
python3 OpenROAD_Fill/src/fin/test/script/replicate_odb_native.py asap7/tpu/tpu.orfs.out.def benchmarks/asap7_tpu_orfs/expanded_odb_native/tpu_orfs_2x2.odb --nx 2 --ny 2 --share-power --build
```

After building once, create a 50x50:

```bash
python3 OpenROAD_Fill/src/fin/test/script/replicate_odb_native.py asap7/tpu/tpu.orfs.out.def benchmarks/asap7_tpu_orfs/expanded_odb_native/tpu_orfs_50x50.odb --nx 50 --ny 50 --share-power
```

Equivalent standalone build command, run inside `OpenROAD_Fill`:

```bash
bazelisk build //src/fin/test/script/odb_native:replicate_odb -c opt --jobs=16
```

The default binary is
`OpenROAD_Fill/bazel-bin/src/fin/test/script/odb_native/replicate_odb`.
`--backend PATH` or `ODB_REPLICATE_BACKEND` selects another build. `--build`
rebuilds the default helper after source updates; `--jobs` controls build jobs.
The helper links this checkout's OpenDB library. Use a matching OpenROAD build to
read the saved ODB; arbitrary ODB schema versions are not interchangeable.

## Inputs and outputs

- Input may be the original routed `.def` (including `.def.gz`) or `.odb`.
  DEF input uses OpenROAD once to load the original and write a small staging
  ODB; no expanded DEF is created. ODB input goes directly to the native helper.
- Default OpenROAD is `OpenROAD_Fill/bazel-bin/openroad` or `OPENROAD_BIN`.
  Default LEFs are the ASAP7 technology and standard-cell files under
  `~/OpenROAD-flow-scripts/flow/platforms/asap7/lef`, or `$ORFS_ROOT`.
  Override with `--openroad PATH` / `--lefs TECH.lef CELLS.lef ...`.
- Output is an uncompressed `.odb`, plus `.odb.json` containing load, copy,
  instance, net, pin, routing, row, write and cleanup times and object counts.
  Optional `--write-def PATH` exports a small-case diagnostic DEF after saving.
- Existing files are rejected. Staging is on the output filesystem and final
  publication uses no-overwrite links. Failed/interrupted runs clean staging on
  ordinary exit; SIGKILL can leave `.replicate_odb_native_*` directories.
- Tile (0,0) retains original names; later tiles have `tCOLUMN_ROW__` prefixes.
  Default offsets use the original die width/height. `--gap-x/y` use DB units.
- `--share-power` shares POWER/GROUND special nets and directly connects cloned
  pins. Omit it to keep power nets separate; already resolved source wildcard
  connections are copied explicitly.
- Native arrays map original object IDs to cloned objects. Pin connection uses
  the original master terminal, avoiding repeated name lookup. Source connection
  and special-wire lists are snapshotted before shared nets are extended.
- Regular routing preserves branches, junctions, endpoint extensions, relative
  RECTs, vias, terminal references, NDRs and mask colors. Pin/special-wire masks
  are supported natively. Shared regular routing uses an immutable snapshot.
- Placement rows, track/GCell patterns, die and cached core area are expanded.
- Copying is serial: the speedup comes from removing interpreter overhead.
  Parallel mutation of shared OpenDB tables is not assumed safe.
- Full flat object counts and final memory remain large. Coordinate overflow
  and 32-bit instance/net/terminal ID capacity are checked before copying.
  Children, regions/groups, blockages, existing fills, BPin vias and octilinear
  special wires are unsupported. This is a routed benchmark replicator, not a
  complete clone of timing/parasitic/antenna/custom-property metadata.

## Reload for fill experiments

```tcl
read_db /home/jocho/OpenFill/benchmarks/asap7_tpu_orfs/expanded_odb_native/tpu_orfs_50x50.odb
```

LEFs are included in ODB. The shared thread-sweep worker supports ODB inputs:

```bash
bash scripts/run_tpu_combined_thread_sweep.sh --sizes 50x50 --input-format odb --input-dir /home/jocho/OpenFill/benchmarks/asap7_tpu_orfs/expanded_odb_native --thread-counts 16 32 --methods min_amount lp_minvar --lp-threads 16 --repeats 1
```

This runs four independent processes sequentially, reloading the original ODB
for each method/thread pair. No filled database is reused as an input. PDLP
stays at 16 threads; candidate/placement change from 16/16 to 32/32. In ODB mode
the worker uses `read_db` and does not preload LEFs. Load time is ODB loading
only; DEF mode load time includes LEFs. `--verify-post-fill-density` is optional
and adds a full geometric density recomputation for comparison.

## Tests and measured results

```bash
python3 OpenROAD_Fill/src/fin/test/script/test_replicate_odb_native.py
```

Six native integration tests exercise 2x2 shared/separate power, DEF/ODB input,
negative coordinates and gaps, branches, extensions, via/RECT and terminal
opcodes, masks, core/die expansion, no-overwrite, overflow, and 5,000 routed nets.
They reuse the existing miniature fixtures with the new CLI, without changing
the old replicator.

Actual TPU 2x1 and 4x4 measurements, byte comparisons, and MIN VAR checks are
under `runs/odb_replication_validation_20261105/` in the workspace. Reported
speedups are small-case measurements, not forecasts for 50x50. NFS load and
cache state were not controlled; one execution per recorded case.

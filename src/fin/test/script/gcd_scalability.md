# GCD scalability experiment

Run from the repository root:

```bash
python3 src/fin/test/script/run_gcd_scalability.py \
  --output-dir src/fin/test/results/gcd_scalability_r4 \
  --resolution 4 --threads 1
```

The default matrix is `1x1`, `2x5`, `10x10`, `20x50`, `100x100` times `density`,
`lp_minvar`, `min_amount`, `lip1`, `lip2`, `lip3` (30 fresh OpenROAD processes).
The original `prefill_bench/gcd_prefill.def` runs first as the `1x1` baseline.
Use `--baseline-def PATH` to select a different original DEF.
Sizes are columns × rows. Cases run sequentially, one repetition by default.
Select PDLP for the LP methods with `--lp-solver pdlp` (default: `glop`).
Use `--lp-threads 8` to give PDLP eight solver threads (default: one).
This is independent of `--threads`, which controls OpenROAD/OpenMP threads.
Use `--candidate-threads 8` to generate tile candidates on eight threads,
independently of the LP backend and `--lp-threads`. The default is one; GUI
debugging or `--trace-candidates` uses one to keep graphics and tracing serial.
Use `--placement-threads 8` for independent per-tile fill selection and geometry
preparation. DB insertion and post-fill density calculation remain serial; the
original fill order and mask assignment are preserved. The default is one.
Other solver parameters retain their defaults.
Add `--repeats 3` for repeated measurements. GNU `time` is required;
GNU `timeout` is needed only if a positive `--timeout` is specified.
Paths are resolved relative to the script; the working directory may be anywhere.
Explicit relative command-line paths are relative to the current directory.

Expanded inputs are `prefill_bench/expanded/gcd_prefill_<size>.def`, with `.def.gz`
accepted if the uncompressed file is absent. Missing inputs are recorded as
`missing_input`; the remaining cases continue. The runner does not generate DEFs.
Use `--dry-run` to inspect the selected inputs/settings without creating outputs.

The runner reuses `gcd_six_method_comparison.tcl`, with these defaults:

- Rules: `src/fin/test/fill.json`; window: 50 µm; resolution: 4.
- Tile density: 0.20–1.00; window density: 0.30–0.60; origin: (0, 0).
- One OpenROAD thread and `OMP_NUM_THREADS=1`.
- Density analysis, report JSON and SVG output disabled for timing.
- No time limit by default (`--timeout 0`). To limit the entire process,
  including LEF/DEF loading, specify `--timeout SECONDS`.
  Timeout cleanup allows another 30 seconds when a limit is enabled.

`density` invokes the existing greedy `density_fill -rules` command; LP density
bounds/window/resolution apply to the five LP methods, as in the original worker.
The inherited `FIN_*` environment is cleared; experiment parameters come from
the runner's command-line options. Other environment variables, such as library
and license paths, remain available. `--openroad` or `OPENROAD_BIN` selects a build.

To include before/after density analysis, add `--analysis`. Add `--write-reports`
to retain LP density reports and, with analysis enabled, density/profile JSONs.
These options change the workload; keep them consistent across comparisons.

Results:

- `summary.csv` and `summary.json`: latest result for each case, updated after
  every case. `fill_seconds` measures the fill Tcl call. `load_seconds` measures
  LEF/DEF loading. `before_analysis_seconds` and `analysis_seconds` measure
  pre-fill and post-fill density analysis separately.
- `wall_seconds`: full process time, including startup, loading and enabled I/O.
- `max_rss_kib`: GNU time maximum resident memory in KiB across the process
  lifetime (including loading/analysis); this is not fill-only memory usage.
- `runs/<size>/<method>/repeat_NNN/attempt_NNN/`: OpenROAD log, resource counters,
  exact invocation/environment, and worker reports. Logs are redirected to disk.
- `experiment.json`: rules/worker/binary SHA-256 hashes and measurement settings.

Completed runs also report separate `fill_count`, `wire_count`,
`regular_wire_count`, and `special_wire_count` in `run_summary.json`,
`summary.csv`, and `summary.json`. `wire_count` is the sum of regular routed
metal segments and special-wire metal boxes (including power/ground), not the
number of nets. Counts cover all routing layers of the entire block after fill,
including pre-existing fills. Vias, pin shapes, and obstructions are excluded.
Per-layer counts are stored in `feature_counts_by_layer` in the JSON summaries.
`openroad.log` includes per-layer `[Layout features]` lines, a JSON
`LAYOUT_FEATURE_COUNTS` line, and total counts on `COMPARISON_RESULT`.
Counting runs even without `--analysis` or `--write-reports`; its time is recorded
as `feature_count_seconds` separately from fill and analysis (included in wall time).

For LP methods, `candidate_generation_seconds` measures elapsed time for the
per-tile candidate generation batch (including scheduling), excluding fillable
region construction, capacity summation, and LP solving. The separate
`candidate_generation_work_seconds` sums individual `makeTileFillCandidates`
call durations; parallel calls overlap, so this can exceed elapsed time.
Both are contained in the existing broader `candidates` stage.
The log reports these per layer immediately after generation, before LP solving,
along with actual `candidate_threads`. The runner copies totals into
`run_summary.json` and the CSV/JSON summaries, with
`candidate_generation_by_layer` in JSON. With `--write-reports`, LP density JSON
includes `runtime_seconds.candidate_generation`, `candidate_generation_work`, and
`candidate_threads`. A timeout during generation has no completed timing for that
layer. The thread sweep forwards `--candidate-threads` unchanged to each solver.

Failures/timeouts do not stop the remaining cases. The runner exits nonzero if
any selected case fails, times out, or has missing input/results. A zero OpenROAD
exit code also requires a valid worker `run_summary.json` to count as success.
Forced termination can be reported as `failed` with exit 137; inspect its log.

Resume with the same measurement settings and output directory:

```bash
python3 src/fin/test/script/run_gcd_scalability.py \
  --output-dir src/fin/test/results/gcd_scalability_r4 \
  --resolution 4 --threads 1 --resume
```

Successful cases are skipped; failed, timed-out, or missing cases are retried in
new attempt directories. You can change the timeout, add repetitions, or select
a subset of sizes/methods. Changed measurement settings, build, worker or rules
require a new output directory. Successful inputs are checked by file size and
modification time on resume; the runner does not hash multi-gigabyte DEFs.

For a smaller initial experiment:

```bash
python3 src/fin/test/script/run_gcd_scalability.py \
  --sizes 2x5 --methods density lip1 \
  --output-dir src/fin/test/results/gcd_scalability_quick
```

To run only the original prefill through all six methods:

```bash
python3 src/fin/test/script/run_gcd_scalability.py \
  --sizes 1x1 --output-dir src/fin/test/results/gcd_scalability_baseline_r4
```

Placement logs include `[Fill LP] placement breakdown` per layer, with
`placement_prepare_seconds`, `placement_prepare_work_seconds`, `db_insert_seconds`,
`post_fill_density_seconds`, and actual `placement_threads`. The runner copies
these fields into `run_summary.json` and summary CSV/JSON, with
`placement_by_layer` in JSON. LP density report runtime fields are
`placement_prepare`, `placement_prepare_work`, `db_insert`, `post_fill_density`,
and `placement_threads`. Preparation elapsed time includes parallel selection,
per-tile shape construction and the ordered merge. Its work time sums only the
per-tile calls, so it overlaps in parallel. These are subsets of `placement`,
not additional fill phases to add to the total.

The separate `[Fill LP] placement select` log measures only choosing candidate
rectangles against each tile's LP target area, before shape construction and
ordered merging. `placement_select_seconds` is the elapsed parallel selection
phase; `placement_select_work_seconds` sums individual tile selection durations.
Both are recorded in run/CSV/JSON summaries, and in the LP density report as
`placement_select` and `placement_select_work`. Geometry construction runs as a
separate parallel phase so it is excluded from these selection measurements.

Placement now emits disjoint functional phases as
`[Fill LP] placement phase layer=... name=... seconds=...`. These are copied into
run and summary CSV/JSON, and into LP density JSON under
`runtime_seconds.placement_phases`. The phases cover:

- `placement_setup_seconds`: per-tile output/buffer allocation.
- `placement_select_seconds`: parallel candidate selection against tile targets.
- `placement_shape_seconds`: parallel construction of selected tile geometry.
- `placement_collect_seconds`: count/reserve/flatten selected rectangle lists.
- `placement_merge_seconds`: merge tile polygon sets in stable order.
- `placement_cleanup_seconds`: release transient per-tile buffers.
- `placement_prepare_other_seconds`: preparation bookkeeping not covered above.
- `placement_copy_seconds`: copy existing layer geometry.
- `placement_union_seconds`: append selected fills to post-fill geometry.
- `db_insert_seconds`: serial DB fill insertion loop (including masks/callbacks).
- `post_fill_density_setup_seconds`: initialize density output buffers.
- `post_fill_union_clip_seconds`: normalize/intersect metal union with the region.
- `post_fill_decompose_seconds`: decompose the covered union into rectangles.
- `post_fill_tile_accumulation_seconds`: build grid edges, distribute rectangle
  coverage, and calculate tile densities.
- `post_fill_window_seconds`: prefix sums and window density calculation.
- `post_fill_density_other_seconds`: density cleanup and remaining bookkeeping.
- `placement_other_seconds`: remaining outer placement work.

These elapsed phases partition `placement_seconds` up to log rounding. Parent
aggregates (`placement_prepare_seconds`, `post_fill_density_seconds`) and the
parallel work-time sums must not be added again. The preparation timer now starts
before allocation and includes transient-buffer destruction; placement timing
stops before the end-of-placement reporting. Earlier measurements used a slightly
different scope, so use fresh runs for comparisons.

## GLOP and PDLP thread comparison

Run the same MinVar inputs sequentially with GLOP (one thread), followed by
PDLP with 1, 2, 4, 8, and 16 threads:

```bash
bash src/fin/test/script/run_gcd_pdlp_thread_sweep.sh
```

Defaults are `10x10`, three repetitions, met2-only rules, resolution 4, and
window 50. OpenROAD/OpenMP stay at one thread. Override workload settings with
runner options, for example `--sizes 10x10 20x20 --repeats 3 --timeout 600`.
Use `--dry-run` to inspect the commands without running the experiment.

GLOP writes `glop_threads_1/summary.csv`; PDLP writes `threads_N/summary.csv`.
Each configuration also writes its own process logs under
`src/fin/test/results/YYYYMMDD/gcd_pdlp_thread_sweep_TIMESTAMP/`.
Compare `fill_seconds` or `wall_seconds` for the same input across repetitions.
For solver-only timing, inspect `solve_seconds` in each `openroad.log`.
The sweep fixes solver selection, OpenROAD threads, LP threads, and per-count
output directories; other runner options are forwarded.

To select a stable output directory or resume an interrupted sweep:

```bash
LP_THREAD_SWEEP_OUTPUT_DIR=/path/to/results \
  bash src/fin/test/script/run_gcd_pdlp_thread_sweep.sh --resume
```

## Candidate thread sweep with PDLP fixed at 16 threads

```bash
bash src/fin/test/script/run_gcd_candidate_thread_sweep.sh
```

This runs candidate threads `1 2 4 8 16`, sequentially, with PDLP fixed to 16
solver threads and OpenROAD/OpenMP defaults fixed to one. Explicit candidate
thread settings determine the candidate OpenMP team independently.
Defaults: `10x10`, `min_amount`, three repeats, timeout 1000 seconds per process,
met2-only rules, resolution 4, window 50, analysis off, and report JSON on.
Runner options such as `--sizes`, `--repeats`, and `--timeout` are forwarded.
The solver, solver threads, candidate threads, and output directory are fixed by
the wrapper after forwarded options.

Override candidate counts with `CANDIDATE_THREAD_COUNTS="1 2 4 8 16 32"` (include
one as the baseline). Set `CANDIDATE_THREAD_SWEEP_OUTPUT_DIR` to choose the root
directory, including when using `--resume`. `--dry-run` prints settings only.

Each `candidate_threads_N` directory contains the usual summaries and logs.
The wrapper produces `candidate_speedup.csv` and `.json` in the root, grouped by
size and method. Speedups are the one-thread median divided by the current
median, separately for candidate generation elapsed time, full fill time, and
process wall time. Candidate efficiency is candidate speedup divided by threads.
Failed runs and successful runs whose actual candidate thread count differs from
the requested count are excluded from medians and counted separately; missing
baselines or measurements produce empty CSV fields / JSON null.

Recalculate speedups from existing results without launching OpenROAD:

```bash
python3 src/fin/test/script/summarize_gcd_candidate_thread_sweep.py /path/to/sweep
```

## Placement thread sweep

```bash
bash src/fin/test/script/run_gcd_placement_thread_sweep.sh
```

Runs placement preparation threads `1 2 4 8 16` sequentially, with PDLP fixed
at 16 solver threads and candidate generation fixed at 8 threads by default.
Defaults are `10x10`, Min Amount, three repeats, timeout 1000 seconds per process,
met2-only rules, analysis off, and JSON reports on. Override the fixed candidate
count with `--candidate-threads N`; it remains the same throughout the sweep.
The placement setting parallelizes candidate selection and geometry preparation;
OpenDB insertion and post-fill density calculation remain serial.

Use `PLACEMENT_THREAD_COUNTS="1 2 4 8 16 32"` to select counts (include one), and
`PLACEMENT_THREAD_SWEEP_OUTPUT_DIR` to choose a root, including for `--resume`.
Each `placement_threads_N` directory contains ordinary run logs and summaries.
The wrapper also prints a comparison table and writes `placement_speedup.csv`
and `.json` with medians for placement preparation, DB insertion, post-fill
density calculation, total placement, full fill, and full process wall time.
It also reports tile selection time separately and its one-thread speedup.
The runner reads total `placement_seconds` from the existing placement-end log.
Speedups compare medians against one placement thread, for the same input,
method, and actual candidate thread count. Failed runs and mismatched actual
placement thread counts are excluded from medians and counted separately.
Missing timings/baselines appear as empty CSV fields or JSON null.

Recalculate from saved results only:

```bash
python3 src/fin/test/script/summarize_gcd_placement_thread_sweep.py /path/to/sweep
```

The placement sweep also writes `placement_breakdown.csv` and `.json`, one row
per configuration/phase with median seconds and percentage of median placement
time. Medians of individual phases need not sum to the median total. The printed
bottleneck and `bottleneck_phase` / `bottleneck_seconds` in `placement_speedup.csv`
identify the largest disjoint phase only when all phase timings are available.
Older/partial results keep these fields empty rather than guessing.

## Failed LP diagnostics

Every LP worker sets `-lp_diagnostics` to `attempt_NNN/lp_failure`, even when
`--write-reports` is off. A failed layer writes `lp_failure_<layer>.json`,
`.tiles.csv`, `.windows.csv`, and `.bound_updates.csv`; a solver-returned failure
also writes `.lp` and `.constraints.csv`. Unknown conflict locations are `null`,
not window zero. Read `diagnosis.conflict_identified` before interpreting a
conflict; bound propagation does not compute an IIS and may leave a conflict
unresolved. Requested/effective minimum window density are both recorded.

The runner now marks logged FIN-0050/0051/0052 errors as `fill_failed`, even if
OpenROAD returns zero and writes a run summary. `--resume` retries such runs,
including older runs previously mislabeled `ok`. Failure diagnostic I/O is recorded separately as `runtime_seconds.diagnostics`
in density reports and is also part of the fill/process time; these runs are not successful fill performance samples.
Timeouts and crashes retain their existing status and may have no diagnostic
files because the solver did not return.

Inspect an existing experiment without rerunning the solver:

```bash
python3 src/fin/test/script/explain_lp_failure.py \
  src/fin/test/results/20261028/gcd_pdlp_thread_sweep_20261028_054329
```

The inspector accepts one or more experiment/attempt directories, logs, or
failure diagnostic JSON files. It shows solver status, timeout/crash/option
errors, saved conflict bounds and coordinates, effective density limits, and
artifact paths. `--json` produces machine-readable output. Logs without detailed
diagnostics cannot identify which constraints conflict; `conflict=null` remains
unresolved. The inspector also flags historical `ok` labels contradicted by
solver error logs.

### Candidate preparation breakdown

`[Fill LP] candidate phase` records disjoint elapsed phases in the following
execution order (also saved to summary CSV/JSON and `runtime_seconds.candidate_phases`
in the LP report):

| Phase | Operation |
|---|---|
| `candidate_bloat_seconds` | Expand existing metal by spacing |
| `candidate_free_subtract_seconds` | Subtract expanded metal from the region |
| `candidate_free_decompose_seconds` | Decompose free geometry into rectangles |
| `candidate_tile_partition_seconds` | Distribute rectangle intersections to tiles |
| `candidate_tile_spacing_seconds` | Apply inset tile boundaries |
| `candidate_tile_prune_seconds` | Prune unusable tile geometry |
| `candidate_generation_seconds` | Generate candidates (parallel elapsed time) |
| `candidate_summary_seconds` | Extract fillable polygons, area and candidate capacity |
| `candidate_other_seconds` | Remaining candidate setup/cleanup |

These leaves partition the `candidates` parent up to log rounding. Tile spacing
and prune are separate parallel phases, controlled by candidate threads. Their
timers measure elapsed time around each worker launch/join, not sums of tile
work time. `candidate_prepare_threads` records the actual preparation thread count.
Graphics or detailed candidate tracing force serial preparation. Older logs may
include `candidate_tile_prepare_other_seconds` for the previous serial timer overhead.
Boost polygon operations may defer geometry normalization until the next operation;
these are operation-boundary timings, not internal geometry-algorithm profiles.
New timings require a rebuilt binary and fresh runs. Historical logs are left blank.

### Solver iterations

After each solve, `[Fill LP] solver statistics layer=... solver=... iterations=...`
records the backend iteration count. GLOP reports simplex iterations; PDLP reports
its SolveLog iteration count. These counts have different meanings and should not
be compared across solver types. Unavailable counts use -1 in logs and null in the
LP report's layer-level `solver_iterations` field. The LP report also includes
`lp_solver` per layer. No solver verbosity or convergence settings are changed.

Run summaries and CSV record `lp_solver` and `lp_solver_iterations` (summed across
layers only when all counts are available). Per-layer values remain in run JSON
under `lp_solver_by_layer`. Historical logs without statistics leave the count
missing. The combined sweep writes a separate iteration table in `comparison.md`
and `solver_iterations.csv`; iteration counts are never treated as seconds.

### PDLP convergence history

Pass `--solver-log` to `run_gcd_scalability.py` or the combined thread sweep to
record backend output in each attempt's `openroad.log`. It defaults to off.
The runner records `FIN_LP_SOLVER_LOG=0/1` in the experiment environment. Direct
Tcl runs can set `FIN_LP_SOLVER_LOG=1` before starting OpenROAD.

A rebuilt OpenROAD binary is required. The option calls `MPSolver::EnableOutput()`
for min-var, min-amount and Lip solvers. PDLP's MPSolver backend then selects
verbosity 3. It does not change tolerances, scaling, presolve, thread counts,
restart policy or termination-check frequency. Logging adds overhead, so enable
it for both objectives when comparing diagnostic runs.

PDLP writes a table at its existing termination checks, not necessarily every
single iteration. Useful columns are:

| Column | Meaning |
|---|---|
| `iter#` | Iteration count |
| `time` | Solver-reported elapsed time |
| `rel_prim_res` | Relative primal residual |
| `rel_dual_res` | Relative dual residual |
| `rel_gap` | Relative primal-dual objective gap |
| `prim_resid`, `dual_resid` | Absolute residuals in the scaled LP model |
| `obj_gap` | Signed primal objective minus dual objective |
| `prim_obj`, `dual_obj` | Primal and dual objectives |

Read these together with the termination criteria and final termination reason
printed by PDLP. In particular, the absolute residuals are not raw layout-density
violations. Convergence history is in the raw log; existing summary CSV/JSON
continue to carry the final solver iteration count.

```bash
for method in min_amount lp_minvar; do
  python3 src/fin/test/script/run_gcd_combined_thread_sweep.py \
    --method "$method" --sizes 20x20 --thread-counts 16 \
    --repeats 1 --solver-log
done
```

### Dual-only tolerance sweep

`--dual-tolerance VALUE` is a PDLP-only override for both absolute and relative
**dual** residual tolerances. It explicitly retains primal residual and objective
gap absolute/relative tolerances at 1e-6. Without the flag, backend defaults are
left untouched. The environment key is `FIN_LP_DUAL_TOLERANCE` (zero = no override).
The solver statistics line and summaries record `lp_dual_tolerance` as evidence
that the rebuilt binary applied the option.

After rebuilding, run:

```bash
bash src/fin/test/script/run_gcd_dual_tolerance_sweep.sh
```

This runs min-amount and min-var, sizes 10x10/20x20, once each at dual=1e-6 and
1e-5, sequentially. PDLP/candidate/placement threads are fixed at 16 (global OMP
is 1). Solver logs and density reports are enabled. Results are grouped under
`gcd_dual_tolerance_sweep_<timestamp>/dual_1e-6` and `dual_1e-5`.
`comparison.md`/`comparison.csv` compare Solve() time, total fill time, iterations,
planned/placed fill area, fill count and actual post-fill tile/window density
ranges and violation counts. Do not confuse `min_violation_window_count` and
`min_violation_tile_count` with diagnostic failure counters named
`window_violation_count` / `tile_violation_count`.

The baseline may already violate post-fill density because continuous LP areas
are rounded to discrete legal rectangles. Compare the absolute quality numbers
and their changes, rather than assuming all violations at 1e-5 are new.
Both objectives use the requested minimum window density without an added guard.

Pass `--dry-run` to inspect settings; `--sizes 20x20 --methods min_amount` limits
the sweep to two runs. `--repeats`, `--openroad`, `--output-dir` and `--resume`
are supported. Missing tolerance evidence or changed binaries between conditions
make cases unverified and cause a nonzero exit; an old binary is not accepted as
proof of a tolerance comparison.

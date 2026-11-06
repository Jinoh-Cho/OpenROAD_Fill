#!/usr/bin/env python3
"""Compare candidate/placement 1/1 versus 16/16 on 10x10 and 20x20."""

import argparse
import csv
import datetime
import json
from pathlib import Path
import statistics
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parents[3]
PHASES = [
    ("ODB/DEF loading", "load_seconds"),
    ("Metal collection threads", "metal_collect_threads"),
    ("Spacing fused into collection (1=yes)", "metal_spacing_fused"),
    ("Tile-local free-area preparation (1=yes)", "candidate_free_area_tiled"),
    ("Metal collection total (includes fused spacing)", "initial_collect_total_seconds"),
    ("  Regular wires", "initial_collect_regular_seconds"),
    ("  Special wires/vias", "initial_collect_special_seconds"),
    ("  Instances", "initial_collect_instances_seconds"),
    ("Initial density total", "initial_density_total_seconds"),
    ("Initial density threads", "initial_density_threads"),
    ("Tile-local initial density (1=yes)", "initial_density_tiled"),
    ("  Initial rectangle distribution", "initial_partition_seconds"),
    ("  Initial tile union/area", "initial_tile_union_area_seconds"),
    ("  Initial density buffer cleanup", "initial_density_cleanup_seconds"),
    ("  Initial source input cleanup", "initial_input_cleanup_seconds"),
    ("  Initial union/clip", "initial_union_clip_seconds"),
    ("  Initial rectangle decomposition", "initial_decompose_seconds"),
    ("  Initial tile area accumulation", "initial_tile_accumulation_seconds"),
    ("Initial geometry/density and other compute", "initial_other_seconds"),
    ("Candidate preparation (total)", "candidates_total_seconds"),
    ("Separate metal spacing expansion (0 when fused)", "candidate_bloat_seconds"),
    ("Free-area subtraction", "candidate_free_subtract_seconds"),
    ("Free-area rectangle decomposition", "candidate_free_decompose_seconds"),
    ("Rectangle distribution to tiles", "candidate_tile_partition_seconds"),
    ("Tile boundary spacing", "candidate_tile_spacing_seconds"),
    ("Tile prune", "candidate_tile_prune_seconds"),
    ("Candidate generation", "candidate_generation_seconds"),
    ("Candidate summary / capacity", "candidate_summary_seconds"),
    ("Candidate other", "candidate_other_seconds"),
    ("LP solve", "lp_solve_seconds"),
    ("Placement preparation (total)", "placement_prepare_seconds"),
    ("  Select", "placement_select_seconds"),
    ("  Shape", "placement_shape_seconds"),
    ("  Collect", "placement_collect_seconds"),
    ("  Merge", "placement_merge_seconds"),
    ("  Cleanup", "placement_cleanup_seconds"),
    ("  Setup", "placement_setup_seconds"),
    ("  Preparation other", "placement_prepare_other_seconds"),
    ("Geometry copy", "placement_copy_seconds"),
    ("Geometry append", "placement_union_seconds"),
    ("DB insertion", "db_insert_seconds"),
    ("Post-fill density (total)", "post_fill_density_seconds"),
    ("  Union/clip", "post_fill_union_clip_seconds"),
    ("  Decompose", "post_fill_decompose_seconds"),
    ("  Tile accumulation", "post_fill_tile_accumulation_seconds"),
    ("  Window density", "post_fill_window_seconds"),
    ("  Full geometry verification (optional)", "post_fill_validation_seconds"),
    ("  Density setup", "post_fill_density_setup_seconds"),
    ("  Density other", "post_fill_density_other_seconds"),
    ("Placement other", "placement_other_seconds"),
    ("Placement (total)", "placement_seconds"),
    ("Fill (total)", "fill_seconds"),
    ("Feature counting after fill", "feature_count_seconds"),
    ("Framework TOTAL (load + fill + postprocessing + exit)", "wall_seconds"),
]


def summarize(root, sizes=("10x10", "20x20"), method="min_amount",
              baseline_threads=1, parallel_threads=16, lp_threads=16):
    thread_counts = (baseline_threads, parallel_threads)
    groups = {}
    for threads in thread_counts:
        path = root / ("candidate_%d_placement_%d" % (threads, threads)) / "summary.csv"
        rows = list(csv.DictReader(path.open())) if path.exists() else []
        for row in rows:
            if row["status"] != "ok" or not row.get("log"):
                continue
            report_path = Path(row["log"]).parent / "lp_fill_report.json"
            if not report_path.exists():
                continue
            runtime = [layer["runtime_seconds"]
                       for layer in json.loads(report_path.read_text())["layers"]]
            row["candidates_total_seconds"] = sum(item["candidates"] for item in runtime)
            row["lp_solve_seconds"] = sum(item["lp_solve"] for item in runtime)
            row["initial_other_seconds"] = sum(
                item["compute"] - item["candidates"] - item["lp_solve"] - item["placement"]
                for item in runtime)
        for size in sizes:
            groups[size, threads] = [
                row for row in rows
                if row["size"] == size and row["status"] == "ok"
                and row.get("method", method) == method
                and int(row.get("candidate_threads") or 0) == threads
                and int(row.get("placement_threads") or 0) == threads
            ]

    def median(size, threads, field):
        values = [float(row[field]) for row in groups[size, threads] if row.get(field)]
        return statistics.median(values) if values else None

    output = []
    iteration_rows = []
    lines = ["# Candidate + placement thread comparison", "",
             "Method: " + method, "",
             "Elapsed seconds only; successful runs with matching thread counts.",
             "LP threads fixed at %d. Parent totals include their subphases; do not add them again." % lp_threads, ""]
    for size in sizes:
        lines += ["## " + size, "",
                  "Valid runs: %d/%d = %d; %d/%d = %d." %
                  (baseline_threads, baseline_threads, len(groups[size, baseline_threads]),
                   parallel_threads, parallel_threads, len(groups[size, parallel_threads])), "",
                  ]
        lines += ["", "Solver iterations (median; total across layers):", "",
                  "| Metric | C%d/P%d | C%d/P%d |" %
                  (baseline_threads, baseline_threads, parallel_threads, parallel_threads), "|---|---:|---:|"]
        solver_names = []
        counts = []
        for threads in thread_counts:
            names = sorted({row["lp_solver"] for row in groups[size, threads]
                            if row.get("lp_solver")})
            solver_names.append(" / ".join(names) if names else "—")
            count = median(size, threads, "lp_solver_iterations")
            counts.append("—" if count is None else "%g" % count)
            iteration_rows.append(dict(size=size, candidate_threads=threads,
                                       placement_threads=threads,
                                       lp_solver=" / ".join(names),
                                       median_iterations=count))
        lines += ["| Solver | %s | %s |" % tuple(solver_names),
                  "| Iterations | %s | %s |" % tuple(counts), "",
                  "Elapsed runtime:", "",
                  "| Phase | C%d/P%d seconds | C%d/P%d seconds | Saved seconds | Speedup |" %
                  (baseline_threads, baseline_threads, parallel_threads, parallel_threads),
                  "|---|---:|---:|---:|---:|"]
        for label, field in PHASES:
            baseline = median(size, baseline_threads, field)
            parallel = median(size, parallel_threads, field)
            is_time = field.endswith("_seconds")
            saved = baseline - parallel if is_time and baseline is not None and parallel is not None else None
            speedup = baseline / parallel if saved is not None and parallel > 0 else None
            output.append(dict(size=size, phase=field,
                               baseline_threads=baseline_threads, parallel_threads=parallel_threads,
                               baseline_seconds=baseline, parallel_seconds=parallel,
                               seconds_1=baseline if baseline_threads == 1 else None,
                               seconds_16=parallel if parallel_threads == 16 else None,
                               saved_seconds=saved, speedup=speedup))
            values = ["—" if value is None else "%.6f" % value
                      for value in (baseline, parallel, saved, speedup)]
            lines.append("| %s | %s |" % (label.strip(), " | ".join(values)))
        lines.append("")
    (root / "comparison.md").write_text("\n".join(lines) + "\n")
    with (root / "comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(output[0]))
        writer.writeheader()
        writer.writerows(output)
    with (root / "solver_iterations.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(iteration_rows[0]))
        writer.writeheader()
        writer.writerows(iteration_rows)
    print("Comparison: " + str(root / "comparison.md"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--method", choices=("min_amount", "lp_minvar"),
                        default="min_amount")
    parser.add_argument("--sizes", nargs="+", choices=("10x10", "20x20"),
                        default=["10x10", "20x20"])
    parser.add_argument("--thread-counts", nargs="+", type=int, choices=(1, 16),
                        default=[1, 16], help="Candidate/placement thread pairs to run")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=1000)
    parser.add_argument("--openroad", type=Path)
    parser.add_argument("--dual-tolerance", type=float)
    parser.add_argument("--solver-log", action="store_true",
                        help="Record solver residuals/objectives/gap in openroad.log")
    parser.add_argument("--verify-post-fill-density", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if len(set(args.thread_counts)) != len(args.thread_counts):
        parser.error("Duplicate thread counts are not allowed")
    if len(set(args.sizes)) != len(args.sizes):
        parser.error("Duplicate sizes are not allowed")
    if args.repeats < 1 or args.timeout < 0:
        parser.error("repeats must be positive; timeout must be nonnegative")
    if args.resume and args.output_dir is None:
        parser.error("--resume requires --output-dir")
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    root = (args.output_dir or REPO_DIR / "src/fin/test/results" / stamp[:8]
            / ("gcd_combined_thread_sweep_" + stamp)).resolve()
    status = 0
    for threads in args.thread_counts:
        destination = root / ("candidate_%d_placement_%d" % (threads, threads))
        command = [sys.executable, str(SCRIPT_DIR / "run_gcd_scalability.py"),
                   "--sizes", *args.sizes, "--methods", args.method,
                   "--rules", str(REPO_DIR / "src/fin/test/fill_met2_only.json"),
                   "--resolution", "4", "--window", "50",
                   "--lp-solver", "pdlp", "--lp-threads", "16", "--threads", "1",
                   "--candidate-threads", str(threads), "--placement-threads", str(threads),
                   "--repeats", str(args.repeats), "--timeout", str(args.timeout),
                   "--write-reports", "--output-dir", str(destination)]
        if args.openroad:
            command += ["--openroad", str(args.openroad.resolve())]
        if args.dual_tolerance is not None:
            command += ["--dual-tolerance", str(args.dual_tolerance)]
        if args.verify_post_fill_density:
            command += ["--verify-post-fill-density"]
        if args.solver_log:
            command.append("--solver-log")
        if args.dry_run:
            command.append("--dry-run")
        if args.resume:
            command.append("--resume")
        print("Candidate/placement threads: %d/%d" % (threads, threads), flush=True)
        result = subprocess.run(command, cwd=str(REPO_DIR))
        if result.returncode in (130, 143) or result.returncode < 0:
            return 130
        if result.returncode:
            status = 1
    if not args.dry_run and root.exists():
        summarize(root, args.sizes, args.method)
    print("Results: " + str(root), flush=True)
    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)

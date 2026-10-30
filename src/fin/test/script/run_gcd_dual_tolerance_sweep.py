#!/usr/bin/env python3
"""Compare PDLP dual tolerances 1e-6/1e-5; primal and gap stay at 1e-6."""

import argparse
import csv
import datetime
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parents[3]
TOLERANCES = ("1e-6", "1e-5")
METRICS = [
    ("Solve() seconds", "solve_seconds"),
    ("LP model + solve seconds", "lp_model_and_solve_seconds"),
    ("Iterations", "lp_solver_iterations"),
    ("Fill seconds", "fill_seconds"),
    ("Planned fill area (DBU^2)", "planned_fill_area"),
    ("Placed fill area (DBU^2)", "placed_fill_area"),
    ("Fill rectangles", "fill_count"),
    ("Minimum post-fill window density", "min_window_density"),
    ("Maximum post-fill window density", "max_window_density"),
    ("Windows below density minimum", "min_violation_window_count"),
    ("Windows above density maximum", "max_violation_window_count"),
    ("Minimum post-fill tile density", "min_post_fill_tile_density"),
    ("Maximum post-fill tile density", "max_post_fill_tile_density"),
    ("Tiles below density minimum", "min_violation_tile_count"),
    ("Tiles above density maximum", "max_violation_tile_count"),
]


def summarize(root, sizes, methods):
    records = []
    binaries = set()
    for tolerance in TOLERANCES:
        directory = root / ("dual_" + tolerance)
        manifest = directory / "experiment.json"
        if manifest.exists():
            binaries.add(json.loads(manifest.read_text())["openroad_sha256"])
        path = directory / "summary.csv"
        rows = list(csv.DictReader(path.open())) if path.exists() else []
        for row in rows:
            record = dict(size=row["size"], method=row["method"], repeat=row["repeat"],
                          dual_tolerance=tolerance, status=row["status"],
                          verified=False, log=row.get("log", ""), error=row.get("error", ""))
            for name in ("lp_solver_iterations", "fill_seconds", "fill_count"):
                record[name] = float(row[name]) if row.get(name) else None
            if row["status"] == "ok":
                actual = row.get("lp_dual_tolerance")
                record["verified"] = bool(actual) and float(actual) == float(tolerance)
                if not record["verified"]:
                    record["error"] = "Tolerance not verified in solver log; rebuild OpenROAD."
                report_path = Path(row["log"]).parent / "lp_fill_report.json"
                if report_path.exists():
                    layers = json.loads(report_path.read_text())["layers"]
                    record.update(
                        planned_fill_area=sum(layer["planned_fill_area"] for layer in layers),
                        placed_fill_area=sum(layer["placed_fill_area"] for layer in layers),
                        lp_model_and_solve_seconds=sum(layer["runtime_seconds"]["lp_solve"] for layer in layers),
                        min_window_density=min(layer["min_density"] for layer in layers),
                        max_window_density=max(layer["max_density"] for layer in layers))
                    for field in ("min_violation_window_count", "max_violation_window_count",
                                  "min_violation_tile_count", "max_violation_tile_count"):
                        if all(field in layer for layer in layers):
                            record[field] = sum(layer[field] for layer in layers)
                    for field, operation in (("min_post_fill_tile_density", min),
                                             ("max_post_fill_tile_density", max)):
                        if all(field in layer for layer in layers):
                            record[field] = operation(layer[field] for layer in layers)
                else:
                    record["verified"] = False
                    record["error"] = "Missing LP density report."
                text = Path(row["log"]).read_text(errors="replace")
                seconds = re.findall(r"\[Fill LP\] \S+ solve end method=\S+ .*?solve_seconds=([0-9.eE+-]+)", text)
                if seconds:
                    record["solve_seconds"] = sum(map(float, seconds))
            records.append(record)
    if len(binaries) > 1:
        for record in records:
            record.update(verified=False, error="Binary changed between tolerance conditions.")

    def selected(size, method, tolerance):
        return [r for r in records if r["size"] == size and r["method"] == method
                and r["dual_tolerance"] == tolerance and r["status"] == "ok" and r["verified"]]

    def median(rows, field):
        values = [r[field] for r in rows if r.get(field) is not None]
        return statistics.median(values) if len(values) == len(rows) and values else None

    def display(value):
        return "—" if value is None else "%.9g" % value

    fields = ["size", "method", "repeat", "dual_tolerance", "status", "verified", "log", "error"]
    fields += [field for _, field in METRICS]
    with (root / "comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(records)
    lines = ["# PDLP dual tolerance comparison", "",
             "Dual absolute/relative tolerance: 1e-6 versus 1e-5.",
             "Primal and objective-gap absolute/relative tolerances remain 1e-6; all worker/solver threads = 16 (global OMP = 1).", "",
             "Values are medians of successful, tolerance-verified runs. Density counts refer to actual post-fill geometry, not diagnostic failure counters.",
             "Existing violations in the baseline must be distinguished from additional violations at 1e-5.", ""]
    valid = True
    for size in sizes:
        for method in methods:
            base = selected(size, method, "1e-6")
            relaxed = selected(size, method, "1e-5")
            valid = valid and bool(base) and bool(relaxed)
            lines += ["## %s / %s" % (size, method), "",
                      "Verified runs: 1e-6 = %d, 1e-5 = %d." % (len(base), len(relaxed)), "",
                      "| Metric | Dual 1e-6 | Dual 1e-5 | 1e-5 minus 1e-6 |",
                      "|---|---:|---:|---:|"]
            for label, field in METRICS:
                a, b = median(base, field), median(relaxed, field)
                delta = b - a if a is not None and b is not None else None
                lines.append("| %s | %s | %s | %s |" % (label, display(a), display(b), display(delta)))
            a, b = median(base, "solve_seconds"), median(relaxed, "solve_seconds")
            if a is not None and b is not None and b > 0:
                lines += ["", "Solve speedup: %.3fx; saved %.6f seconds." % (a / b, a - b)]
            lines.append("")
    errors = [r for r in records if r["status"] != "ok" or not r["verified"]]
    if errors:
        valid = False
        lines += ["## Failed/unverified cases", ""]
        lines += ["- %s %s dual=%s repeat=%s: %s" %
                  (r["size"], r["method"], r["dual_tolerance"], r["repeat"], r["error"] or r["status"]) for r in errors]
    (root / "comparison.md").write_text("\n".join(lines) + "\n")
    print("Comparison: " + str(root / "comparison.md"), flush=True)
    return valid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", nargs="+", default=["10x10", "20x20"])
    parser.add_argument("--methods", nargs="+", choices=("min_amount", "lp_minvar"),
                        default=["min_amount", "lp_minvar"])
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=1000)
    parser.add_argument("--openroad", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.resume and args.output_dir is None:
        parser.error("--resume requires --output-dir")
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    root = (args.output_dir or REPO_DIR / "src/fin/test/results" / stamp[:8]
            / ("gcd_dual_tolerance_sweep_" + stamp)).resolve()
    status = 0
    for tolerance in TOLERANCES:
        command = [sys.executable, str(SCRIPT_DIR / "run_gcd_scalability.py"),
                   "--sizes", *args.sizes, "--methods", *args.methods,
                   "--rules", str(REPO_DIR / "src/fin/test/fill_met2_only.json"),
                   "--resolution", "4", "--window", "50", "--lp-solver", "pdlp",
                   "--lp-threads", "16", "--threads", "1", "--candidate-threads", "16",
                   "--placement-threads", "16", "--dual-tolerance", tolerance, "--solver-log",
                   "--repeats", str(args.repeats), "--timeout", str(args.timeout), "--write-reports",
                   "--output-dir", str(root / ("dual_" + tolerance))]
        if args.openroad:
            command += ["--openroad", str(args.openroad.resolve())]
        if args.resume:
            command.append("--resume")
        if args.dry_run:
            command.append("--dry-run")
        print("Dual absolute/relative tolerance: " + tolerance, flush=True)
        result = subprocess.run(command, cwd=str(REPO_DIR))
        if result.returncode < 0 or result.returncode in (130, 143):
            return 130
        if result.returncode:
            status = 1
    if not args.dry_run and root.exists() and not summarize(root, args.sizes, args.methods):
        status = 1
    print("Results: " + str(root), flush=True)
    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)

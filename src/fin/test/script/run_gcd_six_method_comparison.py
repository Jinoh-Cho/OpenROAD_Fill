#!/usr/bin/env python3
"""Compare six FIN methods using one fresh OpenROAD process per method."""

import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime


METHODS = ("density", "lp_minvar", "min_amount", "lip1", "lip2", "lip3")
SCRIPT_DIR = Path(__file__).resolve().parent
TEST_DIR = SCRIPT_DIR.parent
REPO_DIR = TEST_DIR.parents[2]


def arguments(benchmark="gcd"):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openroad", type=Path, default=REPO_DIR / "bazel-bin/openroad")
    parser.add_argument("--rules", type=Path, default=TEST_DIR / "fill_tiny_pattern.json")
    parser.add_argument("--output", type=Path, help="New directory; existing paths are rejected.")
    parser.add_argument("--resolution", type=int, default=2)
    parser.add_argument("--window", type=float, default=50)
    parser.add_argument("--min-tile-density", type=float, default=0.20)
    parser.add_argument("--max-tile-density", type=float, default=1.0)
    parser.add_argument("--min-window-density", type=float, default=0.30)
    parser.add_argument("--max-window-density", type=float, default=0.60)
    parser.add_argument("--algorithms", nargs="+", choices=("alg2", "alg3"), default=["alg2", "alg3"])
    parser.add_argument("--svg", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="Show configuration without running or writing files.")
    args = parser.parse_args()
    args.benchmark = benchmark
    if not (TEST_DIR / "prefill_bench" / f"{benchmark}_prefill.def").is_file():
        parser.error(f"Prefill DEF not found for {benchmark}")
    if args.resolution <= 0 or not (0 < args.window < float("inf")):
        parser.error("resolution and window must be positive")
    for name in ("min_tile_density", "max_tile_density", "min_window_density", "max_window_density"):
        if not 0 <= getattr(args, name) <= 1:
            parser.error(f"{name} must be in [0, 1]")
    if args.min_tile_density > args.max_tile_density or args.min_window_density > args.max_window_density:
        parser.error("minimum density must not exceed maximum density")
    args.openroad = args.openroad.resolve()
    args.rules = args.rules.resolve()
    if not args.openroad.is_file() or not os.access(args.openroad, os.X_OK):
        parser.error(f"OpenROAD is not executable: {args.openroad}")
    if not args.rules.is_file():
        parser.error(f"Rules file not found: {args.rules}")
    args.output = (args.output or TEST_DIR / "results" / datetime.now().strftime("%Y%m%d")
                   / benchmark / ("six_method_comparison_" + datetime.now().strftime("%H%M%S_%f"))).resolve()
    if args.output.exists():
        parser.error(f"Output already exists: {args.output}")
    return args


def write_summaries(output, runs, rows):
    (output / "summary.json").write_text(json.dumps(runs, indent=2) + "\n")
    fields = ("method", "status", "layer", "fill_seconds", "analysis_seconds",
              "placed_fill_area_dbu2", "added_metal_area_dbu2", "min_density",
              "max_density", "mean_density", "variance", "min_violation_window_count",
              "max_violation_window_count", "floating_min_density", "floating_max_density")
    with (output / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main(benchmark="gcd"):
    args = arguments(benchmark)
    command = [str(args.openroad), "-no_splash", "-no_init", "-exit",
               str(SCRIPT_DIR / f"{benchmark}_six_method_comparison.tcl")]
    config = {key: str(value) if isinstance(value, Path) else value
              for key, value in vars(args).items()}
    config["methods"] = METHODS
    if args.dry_run:
        print(json.dumps(config, indent=2))
        return 0
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "config.json").write_text(json.dumps(config, indent=2) + "\n")
    runs, rows = [], []
    failed = False
    for method in METHODS:
        output = args.output / method
        output.mkdir()
        env = os.environ.copy()
        env.update({"FIN_BENCHMARK": benchmark,
                    "FIN_FILL_METHOD": method, "FIN_OUTPUT_DIR": str(output),
                    "FIN_RULES": str(args.rules), "FIN_RESOLUTION": str(args.resolution),
                    "FIN_WINDOW": str(args.window), "FIN_MIN_TILE_DENSITY": str(args.min_tile_density),
                    "FIN_MAX_TILE_DENSITY": str(args.max_tile_density),
                    "FIN_MIN_WINDOW_DENSITY": str(args.min_window_density),
                    "FIN_MAX_WINDOW_DENSITY": str(args.max_window_density),
                    "FIN_FLOATING_DENSITY_ALGORITHMS": " ".join(args.algorithms),
                    "FIN_WRITE_SVG": str(int(args.svg))})
        print(f"Running {method} ...", flush=True)
        with (output / "run.log").open("w") as log:
            result = subprocess.run(command, cwd=REPO_DIR, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, check=False)
        summary = output / "run_summary.json"
        run = {"method": method, "status": "failed", "returncode": result.returncode,
               "log": str(output / "run.log")}
        try:
            if result.returncode != 0:
                raise RuntimeError(f"OpenROAD exited with {result.returncode}")
            run.update(json.loads(summary.read_text()))
            report = json.loads((output / f"density_{args.algorithms[-1]}.json").read_text())
            profiles = {}
            for profile in output.glob(f"floating_{args.algorithms[-1]}_*.json"):
                data = json.loads(profile.read_text())
                profiles[data["layer"]] = data
            method_rows = []
            for layer in report["layers"]:
                row = {**run, **layer, "status": "ok"}
                profile = profiles[layer["layer"]]
                row["floating_min_density"] = profile["min_density"]
                row["floating_max_density"] = profile["max_density"]
                method_rows.append(row)
            if not method_rows:
                raise RuntimeError("Density report has no layers; check the rules file")
            rows.extend(method_rows)
            run["status"] = "ok"
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            failed = True
            run["error"] = str(error)
            rows.append(run.copy())
        runs.append(run)
        write_summaries(args.output, runs, rows)
        print(f"  {run['status']}: {output / 'run.log'}", flush=True)
    print(f"Summary: {args.output / 'summary.csv'}")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
"""Run the original and expanded GCD DEFs through the six-method Tcl worker.

Requires Python 3.6+ and GNU /usr/bin/time. GNU timeout is needed only when
--timeout is set to a positive value. No Python packages.
Each case uses a fresh OpenROAD process. Max RSS includes loading, filling,
optional analysis, and child processes; it is not fill-only memory usage.
"""

import argparse
import csv
from datetime import datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time


SCRIPT_DIR = Path(__file__).resolve().parent
TEST_DIR = SCRIPT_DIR.parent
REPO_DIR = TEST_DIR.parents[2]
METHODS = ("density", "lp_minvar", "min_amount", "lip1", "lip2", "lip3")
SIZES = ("1x1", "2x5", "10x10", "20x50", "100x100")
# Disjoint wall-time components of placement (no work-time sums or parents).
CANDIDATE_PHASE_FIELDS = (
    "candidate_bloat_seconds", "candidate_free_subtract_seconds",
    "candidate_free_decompose_seconds", "candidate_tile_partition_seconds",
    "candidate_tile_spacing_seconds", "candidate_tile_prune_seconds",
    "candidate_tile_prepare_other_seconds", "candidate_summary_seconds",
    "candidate_other_seconds",
)
PLACEMENT_PHASE_FIELDS = (
    "placement_setup_seconds", "placement_select_seconds", "placement_shape_seconds",
    "placement_collect_seconds", "placement_merge_seconds", "placement_cleanup_seconds",
    "placement_prepare_other_seconds", "placement_copy_seconds", "placement_union_seconds",
    "db_insert_seconds", "post_fill_density_setup_seconds", "post_fill_union_clip_seconds",
    "post_fill_decompose_seconds", "post_fill_tile_accumulation_seconds",
    "post_fill_window_seconds", "post_fill_density_other_seconds", "placement_other_seconds",
)
FIELDS = (
    "size", "tiles", "method", "repeat", "status", "returncode",
    "load_seconds", "before_analysis_seconds", "fill_seconds", "analysis_seconds",
    "wall_seconds", "max_rss_kib", "user_seconds", "system_seconds",
    "placed_fill_area_dbu2", "added_metal_area_dbu2", "input_bytes",
    "fill_count", "wire_count", "regular_wire_count", "special_wire_count",
    "feature_count_seconds", "lp_solver", "lp_solver_iterations", "lp_dual_tolerance",
    "candidate_generation_seconds",
    "candidate_generation_work_seconds", "candidate_threads", "candidate_prepare_threads",
    "placement_prepare_seconds", "placement_prepare_work_seconds",
    "post_fill_density_seconds", "placement_threads", "placement_seconds",
    "placement_select_work_seconds",
    *CANDIDATE_PHASE_FIELDS,
    *PLACEMENT_PHASE_FIELDS,
    "timeout_seconds", "input_def", "log", "error",
)


def atomic_json(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    os.replace(str(temporary), str(path))


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", nargs="+", default=SIZES)
    parser.add_argument("--methods", nargs="+", choices=METHODS, default=METHODS)
    parser.add_argument("--input-dir", type=Path, default=TEST_DIR / "prefill_bench/expanded")
    parser.add_argument("--baseline-def", type=Path, default=TEST_DIR / "prefill_bench/gcd_prefill.def",
                        help="Original DEF used for the 1x1 baseline")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--openroad", type=Path, default=Path(os.environ.get("OPENROAD_BIN", str(REPO_DIR / "bazel-bin/openroad"))))
    parser.add_argument("--rules", type=Path, default=TEST_DIR / "fill.json")
    parser.add_argument("--lp-solver", choices=("glop", "pdlp"), default="glop")
    parser.add_argument("--dual-tolerance", type=float,
                        help="PDLP dual absolute/relative tolerance; primal/gap stay at 1e-6")
    parser.add_argument("--solver-log", action="store_true",
                        help="Record backend convergence statistics in openroad.log")
    parser.add_argument("--lp-threads", type=int, default=1, help="PDLP solver threads (default: 1)")
    parser.add_argument("--candidate-threads", type=int, default=1,
                        help="Independent LP candidate-generation threads (default: 1)")
    parser.add_argument("--placement-threads", type=int, default=1,
                        help="LP candidate selection/geometry threads; DB insertion stays serial (default: 1)")
    parser.add_argument("--resolution", type=int, default=4)
    parser.add_argument("--window", type=float, default=50)
    parser.add_argument("--min-tile-density", type=float, default=0.20)
    parser.add_argument("--max-tile-density", type=float, default=1.0)
    parser.add_argument("--min-window-density", type=float, default=0.30)
    parser.add_argument("--max-window-density", type=float, default=0.60)
    parser.add_argument("--threads", type=int, default=1, help="OpenROAD and OMP threads (default: 1)")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=0, help="Seconds per process; 0 means unlimited (default: 0)")
    parser.add_argument("--analysis", action="store_true", help="Enable before/after density analysis (default: off)")
    parser.add_argument("--write-reports", action="store_true", help="Enable density/profile JSON files (default: off)")
    parser.add_argument("--trace-candidates", action="store_true",
                        help="Flush detailed candidate-generation and placement progress to openroad.log (default: off)")
    parser.add_argument("--algorithms", nargs="+", choices=("alg2", "alg3"), default=("alg2", "alg3"))
    parser.add_argument("--resume", action="store_true", help="Skip successful cases in --output-dir; retry other cases")
    parser.add_argument("--dry-run", action="store_true", help="Print planned cases without creating files")
    args = parser.parse_args()
    if args.resume and args.output_dir is None:
        parser.error("--resume requires --output-dir")
    if (min(args.threads, args.lp_threads, args.candidate_threads, args.placement_threads, args.resolution, args.repeats) < 1
            or not math.isfinite(args.timeout) or not math.isfinite(args.window)
            or args.timeout < 0 or args.window <= 0):
        parser.error("threads/lp-threads/resolution/repeats/window must be positive; timeout must be nonnegative")
    if args.lp_solver != "pdlp" and args.lp_threads != 1:
        parser.error("--lp-threads other than 1 requires --lp-solver pdlp")
    if args.dual_tolerance is not None:
        if args.lp_solver != "pdlp" or not math.isfinite(args.dual_tolerance) or args.dual_tolerance <= 0:
            parser.error("--dual-tolerance must be finite, positive and used with PDLP")
    for low, high in ((args.min_tile_density, args.max_tile_density),
                      (args.min_window_density, args.max_window_density)):
        if not 0 <= low <= high <= 1:
            parser.error("Density bounds must satisfy 0 <= min <= max <= 1")
    if any(not re.fullmatch(r"[1-9]\d*x[1-9]\d*", size) for size in args.sizes):
        parser.error("Sizes must be columns x rows, for example 2x5")
    if len(set(args.sizes)) != len(args.sizes) or len(set(args.methods)) != len(args.methods):
        parser.error("Duplicate sizes/methods are not allowed")
    if args.output_dir is None:
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output_dir = TEST_DIR / "results" / stamp[:8] / "gcd" / ("scalability_" + stamp)
    for name in ("input_dir", "baseline_def", "output_dir", "openroad", "rules"):
        setattr(args, name, getattr(args, name).resolve())
    return args


def worker_env(args):
    return {
        "FIN_LP_SOLVER": args.lp_solver, "FIN_LP_THREADS": str(args.lp_threads),
        "FIN_LP_SOLVER_LOG": str(int(args.solver_log)),
        "FIN_LP_DUAL_TOLERANCE": str(args.dual_tolerance if args.dual_tolerance is not None else 0),
        "FIN_CANDIDATE_THREADS": str(args.candidate_threads),
        "FIN_PLACEMENT_THREADS": str(args.placement_threads),
        "FIN_BENCHMARK": "gcd", "FIN_RULES": str(args.rules),
        "FIN_RESOLUTION": str(args.resolution), "FIN_WINDOW": str(args.window),
        "FIN_MIN_TILE_DENSITY": str(args.min_tile_density),
        "FIN_MAX_TILE_DENSITY": str(args.max_tile_density),
        "FIN_MIN_WINDOW_DENSITY": str(args.min_window_density),
        "FIN_MAX_WINDOW_DENSITY": str(args.max_window_density),
        "FIN_RUN_ANALYSIS": str(int(args.analysis)),
        "FIN_TRACE_CANDIDATES": str(int(args.trace_candidates)),
        "FIN_WRITE_REPORTS": str(int(args.write_reports)), "FIN_WRITE_SVG": "0",
        "FIN_FLOATING_DENSITY_ALGORITHMS": " ".join(args.algorithms),
        "OMP_NUM_THREADS": str(args.threads),
    }


def manifest(args):
    return {
        "schema_version": 1, "input_dir": str(args.input_dir),
        "baseline_def": str(args.baseline_def),
        "openroad": str(args.openroad), "openroad_sha256": digest(args.openroad),
        "runner_sha256": digest(Path(__file__)),
        "worker_sha256": digest(SCRIPT_DIR / "gcd_six_method_comparison.tcl"),
        "rules_sha256": digest(args.rules), "threads": args.threads,
        "environment": worker_env(args),
    }


def input_path(args, size):
    path = args.baseline_def if size == "1x1" else args.input_dir / ("gcd_prefill_" + size + ".def")
    if path.suffix == ".gz":
        return path
    compressed = path.with_suffix(".def.gz")
    return compressed if not path.exists() and compressed.exists() else path


def summarize(root):
    records = [json.loads(path.read_text()) for path in sorted(root.glob("runs/*/*/repeat_*/result.json"))]
    temporary = root / "summary.csv.tmp"
    with temporary.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)
    os.replace(str(temporary), str(root / "summary.csv"))
    atomic_json(root / "summary.json", records)


def timing_metrics(path):
    metrics = {}
    if path.exists():
        for line in path.read_text().splitlines():
            key, separator, value = line.partition("=")
            if separator and key in {"wall_seconds", "max_rss_kib", "user_seconds", "system_seconds"}:
                try:
                    metrics[key] = int(value) if key == "max_rss_kib" else float(value)
                except ValueError:
                    pass
    return metrics


def fill_failure(log_path):
    if not log_path or not Path(log_path).is_file():
        return None
    content = Path(log_path).read_text(errors="replace")
    match = re.search(r"\[ERROR FIN-005[012]\].*", content)
    return match.group(0) if match else None


def candidate_generation_metrics(log_path):
    pattern = re.compile(
        r"\[Fill LP\] candidate generation layer=(\S+) tiles=(\d+) "
        r"candidate_generation_seconds=([0-9.eE+-]+)"
        r"(?: candidate_generation_work_seconds=([0-9.eE+-]+) candidate_threads=(\d+))?")
    phases = {}
    preparation_threads = []
    preparation_pattern = re.compile(
        r"\[Fill LP\] candidate preparation layer=\S+ candidate_prepare_threads=(\d+)")
    phase_pattern = re.compile(
        r"\[Fill LP\] candidate phase layer=(\S+) name=(\S+) seconds=([0-9.eE+-]+)")
    layers = []
    with log_path.open(errors="replace") as stream:
        for line in stream:
            preparation = preparation_pattern.search(line)
            if preparation:
                preparation_threads.append(int(preparation[1]))
            phase = phase_pattern.search(line)
            if phase and phase[2] in CANDIDATE_PHASE_FIELDS:
                phases[phase[2]] = phases.get(phase[2], 0.0) + float(phase[3])
            match = pattern.search(line)
            if match:
                entry = {"layer": match[1], "tiles": int(match[2]),
                         "seconds": float(match[3])}
                if match[4] is not None:
                    entry.update(work_seconds=float(match[4]), threads=int(match[5]))
                layers.append(entry)
    if preparation_threads:
        phases["candidate_prepare_threads"] = max(preparation_threads)
    if not layers:
        return phases
    result = {"candidate_generation_seconds": sum(layer["seconds"] for layer in layers),
              "candidate_generation_by_layer": layers}
    if all("work_seconds" in layer for layer in layers):
        result.update(candidate_generation_work_seconds=sum(layer["work_seconds"] for layer in layers),
                      candidate_threads=max(layer["threads"] for layer in layers))
    result.update(phases)
    return result


def solver_metrics(log_path):
    pattern = re.compile(
        r"\[Fill LP\] solver statistics layer=(\S+) solver=(\S+) iterations=(-?\d+)"
        r"(?: dual_tolerance=([0-9.eE+-]+))?")
    layers = []
    with log_path.open(errors="replace") as stream:
        for line in stream:
            match = pattern.search(line)
            if match:
                count = int(match[3])
                layers.append({"layer": match[1], "solver": match[2],
                               "iterations": count if count >= 0 else None})
                if match[4] is not None:
                    layers[-1]["dual_tolerance"] = float(match[4])
    if not layers:
        return {}
    solvers = sorted({layer["solver"] for layer in layers})
    result = {"lp_solver": "+".join(solvers), "lp_solver_by_layer": layers}
    tolerances = {layer.get("dual_tolerance") for layer in layers}
    if len(tolerances) == 1 and None not in tolerances:
        result["lp_dual_tolerance"] = tolerances.pop()
    if all(layer["iterations"] is not None for layer in layers):
        result["lp_solver_iterations"] = sum(layer["iterations"] for layer in layers)
    return result


def placement_metrics(log_path):
    pattern = re.compile(
        r"\[Fill LP\] placement breakdown layer=(\S+) "
        r"placement_prepare_seconds=([0-9.eE+-]+) "
        r"placement_prepare_work_seconds=([0-9.eE+-]+) "
        r"db_insert_seconds=([0-9.eE+-]+) "
        r"post_fill_density_seconds=([0-9.eE+-]+) placement_threads=(\d+)")
    layers = []
    end_pattern = re.compile(
        r"\[Fill LP\] placement end layer=(\S+) .*?placement_seconds=([0-9.eE+-]+)")
    durations = {}
    selections = {}
    phases = {}
    phase_pattern = re.compile(
        r"\[Fill LP\] placement phase layer=(\S+) name=([a-z_]+_seconds) seconds=([0-9.eE+-]+)")
    select_pattern = re.compile(
        r"\[Fill LP\] placement select layer=(\S+) tiles=\d+ "
        r"placement_select_seconds=([0-9.eE+-]+) "
        r"placement_select_work_seconds=([0-9.eE+-]+) placement_threads=\d+")
    fields = ("placement_prepare_seconds", "placement_prepare_work_seconds",
              "db_insert_seconds", "post_fill_density_seconds")
    with log_path.open(errors="replace") as stream:
        for line in stream:
            phase = phase_pattern.search(line)
            if phase and phase[2] in PLACEMENT_PHASE_FIELDS:
                phases.setdefault(phase[1], {})[phase[2]] = float(phase[3])
            selection = select_pattern.search(line)
            if selection:
                selections[selection[1]] = {
                    "placement_select_seconds": float(selection[2]),
                    "placement_select_work_seconds": float(selection[3])}
            end = end_pattern.search(line)
            if end:
                durations[end[1]] = float(end[2])
            match = pattern.search(line)
            if match:
                entry = {"layer": match[1], "placement_threads": int(match[6])}
                entry.update({field: float(match[i + 2]) for i, field in enumerate(fields)})
                layers.append(entry)
    if not layers:
        return {}
    result = {field: sum(layer[field] for layer in layers) for field in fields}
    if all(layer["layer"] in selections for layer in layers):
        for layer in layers:
            layer.update(selections[layer["layer"]])
        for field in ("placement_select_seconds", "placement_select_work_seconds"):
            result[field] = sum(layer[field] for layer in layers)
    for layer in layers:
        layer.update(phases.get(layer["layer"], {}))
    for field in PLACEMENT_PHASE_FIELDS:
        if all(field in layer for layer in layers):
            result[field] = sum(layer[field] for layer in layers)
    if all(layer["layer"] in durations for layer in layers):
        for layer in layers:
            layer["placement_seconds"] = durations[layer["layer"]]
        result["placement_seconds"] = sum(layer["placement_seconds"] for layer in layers)
    result.update(placement_threads=max(layer["placement_threads"] for layer in layers),
                  placement_by_layer=layers)
    return result


def execute(args, case_dir, record):
    attempt = 1
    while (case_dir / ("attempt_%03d" % attempt)).exists():
        attempt += 1
    output = case_dir / ("attempt_%03d" % attempt)
    output.mkdir()
    record["log"] = str(output / "openroad.log")
    env = {key: value for key, value in os.environ.items() if not key.startswith("FIN_")}
    env.update(worker_env(args))
    env.update(FIN_FILL_METHOD=record["method"], FIN_INPUT_DEF=record["input_def"], FIN_OUTPUT_DIR=str(output))
    command = [str(args.openroad), "-no_init", "-no_splash", "-exit", "-threads", str(args.threads),
               str(SCRIPT_DIR / "gcd_six_method_comparison.tcl")]
    if args.timeout:
        command = ["timeout", "--foreground", "--signal=TERM", "--kill-after=30s", str(args.timeout) + "s"] + command
    command = ["/usr/bin/time", "-o", str(output / "resources.txt"), "-f",
               "wall_seconds=%e\nmax_rss_kib=%M\nuser_seconds=%U\nsystem_seconds=%S"] + command
    atomic_json(output / "invocation.json", {"command": command, "environment": {**worker_env(args),
                "FIN_FILL_METHOD": record["method"], "FIN_INPUT_DEF": record["input_def"], "FIN_OUTPUT_DIR": str(output)},
                "timeout_seconds": args.timeout})
    started = time.monotonic()
    interrupted = False
    with (output / "openroad.log").open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   cwd=str(REPO_DIR), env=env, start_new_session=True)
        try:
            returncode = process.wait()
        except KeyboardInterrupt:
            interrupted = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                returncode = process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                returncode = process.wait()
    record.update(returncode=returncode, wall_seconds=time.monotonic() - started)
    record.update(timing_metrics(output / "resources.txt"))
    generation_metrics = candidate_generation_metrics(output / "openroad.log")
    generation_metrics.update(placement_metrics(output / "openroad.log"))
    generation_metrics.update(solver_metrics(output / "openroad.log"))
    record.update(generation_metrics)
    if interrupted:
        record["status"] = "interrupted"
    elif args.timeout and returncode == 124:
        record["status"] = "timeout"
    elif returncode != 0:
        record["status"] = "failed"
        record["error"] = "Process exited with code %s; inspect openroad.log" % returncode
    else:
        try:
            summary = json.loads((output / "run_summary.json").read_text())
            if summary["method"] != record["method"] or not isinstance(summary["fill_seconds"], (int, float)):
                raise ValueError("Unexpected method or fill_seconds")
            if generation_metrics:
                summary.update(generation_metrics)
                atomic_json(output / "run_summary.json", summary)
            record.update({key: summary[key] for key in FIELDS if key in summary and key != "method"})
            if "feature_counts_by_layer" in summary:
                record["feature_counts_by_layer"] = summary["feature_counts_by_layer"]
            failure = fill_failure(output / "openroad.log")
            if failure:
                record.update(status="fill_failed", error=failure)
            else:
                record["status"] = "ok"
        except (OSError, ValueError, KeyError) as error:
            record.update(status="missing_summary", error=str(error))
    return interrupted


def run_case(args, size, method, repeat):
    case_dir = args.output_dir / "runs" / size / method / ("repeat_%03d" % repeat)
    path = input_path(args, size)
    fingerprint = {"bytes": path.stat().st_size, "mtime_ns": path.stat().st_mtime_ns} if path.is_file() else None
    result_path = case_dir / "result.json"
    if args.resume and result_path.exists():
        previous = json.loads(result_path.read_text())
        if previous["status"] == "ok" and not fill_failure(previous.get("log")):
            if previous.get("input_fingerprint") != fingerprint:
                raise ValueError("Input changed since successful run: " + str(path))
            print("SKIP %s %s repeat=%d (already successful)" % (size, method, repeat), flush=True)
            return True, False
    case_dir.mkdir(parents=True, exist_ok=True)
    nx, ny = map(int, size.split("x"))
    record = {"size": size, "tiles": nx * ny, "method": method, "repeat": repeat,
              "input_def": str(path), "input_fingerprint": fingerprint,
              "input_bytes": fingerprint["bytes"] if fingerprint else None,
              "timeout_seconds": args.timeout, "started_at": datetime.now().isoformat()}
    print("RUN %s %s repeat=%d" % (size, method, repeat), flush=True)
    interrupted = False
    if fingerprint is None:
        record.update(status="missing_input", error="Input file does not exist")
    else:
        interrupted = execute(args, case_dir, record)
    record["finished_at"] = datetime.now().isoformat()
    atomic_json(result_path, record)
    summarize(args.output_dir)
    print("  %s fill_s=%s wall_s=%s max_rss_kib=%s" %
          (record["status"], record.get("fill_seconds", "n/a"), record.get("wall_seconds", "n/a"),
           record.get("max_rss_kib", "n/a")), flush=True)
    return record["status"] == "ok", interrupted


def main():
    args = parse_args()
    if not args.openroad.is_file() or not os.access(str(args.openroad), os.X_OK):
        raise ValueError("OpenROAD executable not found: " + str(args.openroad))
    if not args.rules.is_file():
        raise ValueError("Rules not found: " + str(args.rules))
    if not Path("/usr/bin/time").is_file():
        raise ValueError("GNU /usr/bin/time is required")
    if args.timeout and not shutil.which("timeout"):
        raise ValueError("GNU timeout is required when --timeout is positive")
    print("Results: " + str(args.output_dir), flush=True)
    if args.dry_run:
        for size in args.sizes:
            path = input_path(args, size)
            for method in args.methods:
                print("%s %s repeats=%d input=%s%s" %
                      (size, method, args.repeats, shlex.quote(str(path)), " [MISSING]" if not path.is_file() else ""))
        print("Settings: " + json.dumps(worker_env(args), sort_keys=True))
        return 0
    config = manifest(args)
    config_path = args.output_dir / "experiment.json"
    if args.resume:
        if not config_path.is_file() or json.loads(config_path.read_text()) != config:
            raise ValueError("Cannot resume: experiment configuration/build changed or experiment.json is missing")
    else:
        if args.output_dir.exists():
            raise ValueError("Output directory exists; choose a new directory or use --resume")
        args.output_dir.mkdir(parents=True)
        atomic_json(config_path, config)
    with (args.output_dir / ".runner.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("Another runner is using this output directory")
        success = True
        for size in args.sizes:
            for method in args.methods:
                for repeat in range(1, args.repeats + 1):
                    ok, interrupted = run_case(args, size, method, repeat)
                    success = success and ok
                    if interrupted:
                        return 130
        summarize(args.output_dir)
    print("Summary: " + str(args.output_dir / "summary.csv"), flush=True)
    return 0 if success else 1


def interrupt_run(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt_run)
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError) as error:
        sys.exit("error: " + str(error))

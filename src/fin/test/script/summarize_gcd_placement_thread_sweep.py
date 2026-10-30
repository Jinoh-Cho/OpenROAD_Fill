#!/usr/bin/env python3
"""Summarize saved placement-thread sweeps without launching OpenROAD."""

import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics

from run_gcd_scalability import PLACEMENT_PHASE_FIELDS

TIMINGS = (
    "placement_select_seconds", "placement_select_work_seconds",
    "placement_prepare_seconds", "placement_prepare_work_seconds",
    "db_insert_seconds", "post_fill_density_seconds", "placement_seconds",
    "candidate_generation_seconds", "fill_seconds", "wall_seconds",
)
TIMINGS += tuple(field for field in PLACEMENT_PHASE_FIELDS if field not in TIMINGS)


def median(records, field):
    values = [r[field] for r in records
              if isinstance(r.get(field), (int, float))
              and math.isfinite(r[field]) and r[field] >= 0]
    return statistics.median(values) if values else None


def summarize(root):
    rows = []
    for path in root.glob("placement_threads_*/summary.json"):
        match = re.fullmatch(r"placement_threads_([1-9]\d*)", path.parent.name)
        if not match:
            continue
        threads = int(match[1])
        records = json.loads(path.read_text())
        # Candidate thread count is part of the series, so different candidate
        # settings cannot accidentally share a one-placement-thread baseline.
        groups = {(r["size"], r["method"], r.get("candidate_threads")) for r in records}
        for size, method, candidates in sorted(groups, key=lambda g: (g[0], g[1], g[2] or 0)):
            group = [r for r in records
                     if (r["size"], r["method"], r.get("candidate_threads"))
                     == (size, method, candidates)]
            valid = [r for r in group if r.get("status") == "ok"
                     and r.get("placement_threads") == threads]
            row = dict(size=size, method=method, candidate_threads=candidates,
                       placement_threads=threads, valid_runs=len(valid),
                       failed_runs=sum(r.get("status") != "ok" for r in group),
                       thread_mismatch_runs=sum(r.get("status") == "ok"
                                                and r.get("placement_threads") != threads for r in group))
            for field in TIMINGS:
                row["median_" + field] = median(valid, field)
            rows.append(row)
    rows.sort(key=lambda r: (r["size"], r["method"], r["candidate_threads"] or 0,
                             r["placement_threads"]))
    def key(row):
        return row["size"], row["method"], row["candidate_threads"]
    baselines = {key(r): r for r in rows if r["placement_threads"] == 1}
    for row in rows:
        baseline = baselines.get(key(row), {})
        for label, field in (("placement_select", "placement_select_seconds"),
                             ("placement_prepare", "placement_prepare_seconds"),
                             ("placement", "placement_seconds"),
                             ("fill", "fill_seconds"), ("wall", "wall_seconds")):
            base, current = baseline.get("median_" + field), row["median_" + field]
            row[label + "_speedup"] = base / current if base and current else None
        speedup = row["placement_prepare_speedup"]
        row["placement_prepare_efficiency"] = speedup / row["placement_threads"] if speedup else None
        speedup = row["placement_select_speedup"]
        row["placement_select_efficiency"] = speedup / row["placement_threads"] if speedup else None
        values = {field: row["median_" + field] for field in PLACEMENT_PHASE_FIELDS}
        complete = all(value is not None for value in values.values())
        bottleneck = max(values, key=values.get) if complete else None
        row["bottleneck_phase"] = bottleneck
        row["bottleneck_seconds"] = values[bottleneck] if bottleneck else None
    return rows


def breakdown(rows):
    result = []
    for row in rows:
        total = row["median_placement_seconds"]
        for phase in PLACEMENT_PHASE_FIELDS:
            value = row["median_" + phase]
            result.append(dict(size=row["size"], method=row["method"],
                               candidate_threads=row["candidate_threads"],
                               placement_threads=row["placement_threads"], phase=phase,
                               median_seconds=value,
                               percent_of_placement=100 * value / total
                               if value is not None and total else None))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    rows = summarize(args.results)
    if not rows:
        parser.error("No placement_threads_*/summary.json records found")
    csv_path = args.results / "placement_speedup.csv"
    with csv_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (args.results / "placement_speedup.json").write_text(json.dumps(rows, indent=2) + "\n")
    phases = breakdown(rows)
    phase_path = args.results / "placement_breakdown.csv"
    with phase_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(phases[0]))
        writer.writeheader()
        writer.writerows(phases)
    (args.results / "placement_breakdown.json").write_text(json.dumps(phases, indent=2) + "\n")
    def display(value):
        return "n/a" if value is None else "%.3f" % value
    print("size method candidate_threads placement_threads valid_runs select_s prepare_s placement_s fill_s wall_s select_speedup prepare_speedup placement_speedup fill_speedup wall_speedup")
    for r in rows:
        print("%s %s %s %d %d %s" % (
            r["size"], r["method"], str(r["candidate_threads"]), r["placement_threads"],
            r["valid_runs"], " ".join(display(r[field]) for field in (
                "median_placement_select_seconds", "median_placement_prepare_seconds", "median_placement_seconds",
                "median_fill_seconds", "median_wall_seconds", "placement_select_speedup", "placement_prepare_speedup",
                "placement_speedup", "fill_speedup", "wall_speedup"))))
    print("Speedup CSV: " + str(csv_path))
    print("\nPlacement bottlenecks (largest disjoint phase, medians):")
    for row in rows:
        print("%s %s candidates=%s placement=%d: %s %s s" % (
            row["size"], row["method"], row["candidate_threads"], row["placement_threads"],
            row["bottleneck_phase"] or "n/a", display(row["bottleneck_seconds"])))
    print("Phase breakdown CSV: " + str(phase_path))


if __name__ == "__main__":
    main()

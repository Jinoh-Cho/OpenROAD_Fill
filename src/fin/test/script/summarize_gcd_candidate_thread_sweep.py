#!/usr/bin/env python3
"""Summarize saved candidate-thread sweeps; never launches OpenROAD."""

import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics


def median(records, field):
    values = [r[field] for r in records
              if isinstance(r.get(field), (int, float))
              and math.isfinite(r[field]) and r[field] >= 0]
    return statistics.median(values) if values else None


def summarize(root):
    rows = []
    for path in root.glob("candidate_threads_*/summary.json"):
        match = re.fullmatch(r"candidate_threads_([1-9]\d*)", path.parent.name)
        if not match:
            continue
        threads = int(match[1])
        records = json.loads(path.read_text())
        for size, method in sorted({(r["size"], r["method"]) for r in records}):
            group = [r for r in records if (r["size"], r["method"]) == (size, method)]
            valid = [r for r in group if r.get("status") == "ok"
                     and r.get("candidate_threads") == threads]
            row = dict(size=size, method=method, candidate_threads=threads,
                       valid_runs=len(valid), failed_runs=sum(r.get("status") != "ok" for r in group),
                       thread_mismatch_runs=sum(r.get("status") == "ok"
                                                and r.get("candidate_threads") != threads for r in group))
            for field in ("candidate_generation_seconds", "candidate_generation_work_seconds",
                          "fill_seconds", "wall_seconds"):
                row["median_" + field] = median(valid, field)
            rows.append(row)
    rows.sort(key=lambda r: (r["size"], r["method"], r["candidate_threads"]))
    baselines = {(r["size"], r["method"]): r for r in rows if r["candidate_threads"] == 1}
    for row in rows:
        baseline = baselines.get((row["size"], row["method"]), {})
        for label, field in (("candidate", "candidate_generation_seconds"),
                             ("fill", "fill_seconds"), ("wall", "wall_seconds")):
            base = baseline.get("median_" + field)
            current = row["median_" + field]
            row[label + "_speedup"] = base / current if base and current else None
        speedup = row["candidate_speedup"]
        row["candidate_efficiency"] = speedup / row["candidate_threads"] if speedup else None
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    rows = summarize(args.results)
    if not rows:
        parser.error("No candidate_threads_*/summary.json records found")
    csv_path = args.results / "candidate_speedup.csv"
    with csv_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (args.results / "candidate_speedup.json").write_text(json.dumps(rows, indent=2) + "\n")
    print("size method candidate_threads valid_runs candidate_s candidate_speedup fill_speedup")
    def display(value):
        return "n/a" if value is None else "%.3f" % value
    for r in rows:
        print("%s %s %d %d %s %s %s" % (
            r["size"], r["method"], r["candidate_threads"], r["valid_runs"],
            display(r["median_candidate_generation_seconds"]),
            display(r["candidate_speedup"]), display(r["fill_speedup"])))
    print("Speedup CSV: " + str(csv_path))


if __name__ == "__main__":
    main()

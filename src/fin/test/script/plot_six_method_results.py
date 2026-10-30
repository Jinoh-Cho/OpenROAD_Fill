#!/usr/bin/env python3
"""Plot existing six-method floating-density results for GCD and IBEX."""

import csv
import html
import json
import sys
from pathlib import Path


METHODS = ["density", "lp_minvar", "min_amount", "lip1", "lip2", "lip3"]
LABELS = {
    "density": "Density fill",
    "lp_minvar": "LP min-var",
    "min_amount": "LP min-amount",
    "lip1": "Lip1",
    "lip2": "Lip2",
    "lip3": "Lip3",
}
COLORS = {
    "density": "#1f77b4",
    "lp_minvar": "#d62728",
    "min_amount": "#ff7f0e",
    "lip1": "#2ca02c",
    "lip2": "#9467bd",
    "lip3": "#17becf",
}
DASHES = ["", "8 3", "3 3", "12 3 2 3", "1 3", "6 2 1 2", "10 2 2 2"]
WIDTH, HEIGHT = 1600, 980
LEFT, RIGHT, TOP, BOTTOM = 105, 40, 100, 150


def downsample(points, min_x, max_x, bins=1400):
    """Keep local extrema at display-pixel resolution, preserving spikes."""
    if len(points) <= bins * 2:
        return points
    span = max(max_x - min_x, 1)
    buckets = {}
    for x, y in points:
        index = min(bins - 1, int((x - min_x) * bins / span))
        bucket = buckets.setdefault(index, [None, None])
        if bucket[0] is None or y < bucket[0][1]:
            bucket[0] = (x, y)
        if bucket[1] is None or y > bucket[1][1]:
            bucket[1] = (x, y)
    reduced = []
    for index in sorted(buckets):
        reduced.extend(sorted(set(buckets[index])))
    return reduced


def make_graph(bench, result_dir, layer, output):
    profiles = {}
    first = None
    min_x = max_x = None
    for method in METHODS:
        path = result_dir / method / f"floating_alg2_{layer}.json"
        report = json.loads(path.read_text())
        raw_profiles = report.get("profiles", [])
        if first is None:
            first = report
            points = (point for p in raw_profiles for point in p.get("points", []))
            min_x = min(point[0] for point in points)
            points = (point for p in raw_profiles for point in p.get("points", []))
            max_x = max(point[0] for point in points)
        profiles[method] = [
            {"y": p.get("y", 0), "points": downsample(p.get("points", []), min_x, max_x)}
            for p in raw_profiles
        ]
        del report, raw_profiles
    if first is None or min_x is None or max_x is None:
        raise RuntimeError(f"No profile points found under {result_dir}")
    plot_w = WIDTH - LEFT - RIGHT
    plot_h = HEIGHT - TOP - BOTTOM

    def sx(x):
        return LEFT + (x - min_x) * plot_w / max(max_x - min_x, 1)

    def sy(y):
        return TOP + (1.0 - y) * plot_h

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{WIDTH / 2}" y="42" text-anchor="middle" '
        'font-family="sans-serif" font-size="25" font-weight="bold">'
        f'{html.escape(bench.upper())} {layer.upper()} Continuous Density Profile — Six Methods</text>',
        f'<text x="{WIDTH / 2}" y="72" text-anchor="middle" '
        'font-family="sans-serif" font-size="14">window=50, resolution=2; ALG2 profiles; '
        'each trace is a fixed-y sliding-window scan</text>',
        f'<rect x="{LEFT}" y="{TOP}" width="{plot_w}" height="{plot_h}" '
        'fill="#fafafa" stroke="#333"/>',
    ]
    for tick in range(11):
        density = tick / 10
        y = sy(density)
        out.extend(
            [
                f'<line x1="{LEFT}" y1="{y:.2f}" x2="{LEFT + plot_w}" '
                f'y2="{y:.2f}" stroke="#dedede"/>',
                f'<text x="{LEFT - 12}" y="{y + 4:.2f}" text-anchor="end" '
                f'font-family="monospace" font-size="12">{density:.1f}</text>',
            ]
        )
    for density, label, color in (
        (first.get("min_density_limit", 0.30), "input min", "#1565c0"),
        (first.get("max_density_limit", 0.60), "input max", "#c62828"),
    ):
        y = sy(density)
        out.append(
            f'<line x1="{LEFT}" y1="{y:.2f}" x2="{LEFT + plot_w}" '
            f'y2="{y:.2f}" stroke="{color}" stroke-width="1.4" '
            'stroke-dasharray="8 5"/>'
        )
        out.append(
            f'<text x="{LEFT + plot_w - 8}" y="{y - 5:.2f}" text-anchor="end" '
            f'fill="{color}" font-family="monospace" font-size="12">'
            f'{label}={density:.2f}</text>'
        )

    for method in METHODS:
        for index, profile in enumerate(
            sorted(profiles[method], key=lambda p: p.get("y", 0), reverse=True)
        ):
            points = downsample(profile.get("points", []), min_x, max_x)
            polyline = " ".join(f"{sx(x):.2f},{sy(d):.2f}" for x, d in points)
            dash = DASHES[index % len(DASHES)]
            dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
            out.append(
                f'<polyline points="{polyline}" fill="none" '
                f'stroke="{COLORS[method]}" stroke-width="0.8" '
                f'stroke-opacity="0.48"{dash_attr}/>'
            )

    out.extend(
        [
            f'<text x="{LEFT + plot_w / 2}" y="{HEIGHT - 82}" text-anchor="middle" '
            'font-family="sans-serif" font-size="15">window left x (DBU)</text>',
            f'<text x="25" y="{TOP + plot_h / 2}" '
            f'transform="rotate(-90 25 {TOP + plot_h / 2})" text-anchor="middle" '
            'font-family="sans-serif" font-size="15">density</text>',
        ]
    )
    legend_y = HEIGHT - 32
    for index, method in enumerate(METHODS):
        row, col = divmod(index, 3)
        x = LEFT + 65 + col * 450
        y = legend_y + row * 26
        out.append(
            f'<line x1="{x}" y1="{y - 5}" x2="{x + 38}" y2="{y - 5}" '
            f'stroke="{COLORS[method]}" stroke-width="2.4"/>'
        )
        out.append(
            f'<text x="{x + 48}" y="{y}" fill="{COLORS[method]}" '
            f'font-family="sans-serif" font-size="14">{LABELS[method]}</text>'
        )
    out.append("</svg>")
    output.write_text("\n".join(out) + "\n")


def make_tables(result_dirs, date_root):
    rows = []
    layer_rows = []
    for bench, result_dir in result_dirs.items():
        with (result_dir / "summary.csv").open(newline="") as stream:
            data = list(csv.DictReader(stream))
        layer_rows.extend({"benchmark": bench, **row} for row in data)
        for method in METHODS:
            subset = [row for row in data if row["method"] == method]
            if not subset:
                continue
            m2 = next(row for row in subset if row["layer"] == "met2")
            all_min = min(float(row["floating_min_density"]) for row in subset)
            all_max = max(float(row["floating_max_density"]) for row in subset)
            violation_min = sum(int(row["min_violation_window_count"]) for row in subset)
            violation_max = sum(int(row["max_violation_window_count"]) for row in subset)
            row = {
                "benchmark": bench,
                "method": method,
                "fill_seconds": subset[0]["fill_seconds"],
                "analysis_seconds": subset[0]["analysis_seconds"],
                "added_metal_area_dbu2": subset[0]["added_metal_area_dbu2"],
                "met2_floating_min": m2["floating_min_density"],
                "met2_floating_max": m2["floating_max_density"],
                "all_layers_floating_min": f"{all_min:.8f}",
                "all_layers_floating_max": f"{all_max:.8f}",
                "fixed_window_min_violations": str(violation_min),
                "fixed_window_max_violations": str(violation_max),
            }
            rows.append(row)

    columns = list(rows[0])
    with (date_root / "six_method_comparison_summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)
    layer_columns = list(layer_rows[0])
    with (date_root / "six_method_comparison_layer_metrics.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=layer_columns)
        writer.writeheader()
        writer.writerows(layer_rows)

    md = [
        "# Six-method comparison (2026-10-09)",
        "",
        "Source runs use `fill_tiny_pattern.json`, resolution 2, 50-unit windows, "
        "min/max window density 0.30/0.60. MET2 ranges below are exact ALG2 "
        "continuous sliding-window profile extrema; ALG3 results are also present "
        "in the source runs.",
        "",
        "| Design | Method | Fill (s) | Density analysis (s) | Added metal area (DBU²) | MET2 floating min–max | All-layer floating min–max | Fixed-window min/max violations |",
        "|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for r in rows:
        area = float(r["added_metal_area_dbu2"])
        md.append(
            f"| {r['benchmark'].upper()} | {LABELS[r['method']]} | {float(r['fill_seconds']):.3f} | "
            f"{float(r['analysis_seconds']):.3f} | {area:,.0f} | "
            f"{float(r['met2_floating_min']):.4f}–{float(r['met2_floating_max']):.4f} | "
            f"{float(r['all_layers_floating_min']):.4f}–{float(r['all_layers_floating_max']):.4f} | "
            f"{r['fixed_window_min_violations']} / {r['fixed_window_max_violations']} |"
        )
    md.extend(
        [
            "",
            "Analysis time is the aggregate reported by the run summary for both "
            "floating-density algorithms and all five metal layers. Fixed-window "
            "violations are summed over MET1–MET5; continuous extrema are measured "
            "separately and need not obey the fixed-window limits.",
            "",
        ]
    )
    (date_root / "six_method_comparison_summary.md").write_text("\n".join(md))


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} results_20261009_directory")
    date_root = Path(sys.argv[1]).resolve()
    result_dirs = {}
    for bench in ("gcd", "ibex"):
        candidates = sorted((date_root / bench).glob("six_method_comparison_*/summary.csv"))
        if not candidates:
            continue
        result_dir = candidates[-1].parent
        result_dirs[bench] = result_dir
        for layer in ("met1", "met2", "met3", "met4", "met5"):
            graph = result_dir / f"{bench}_{layer}_six_method_comparison.svg"
            make_graph(bench, result_dir, layer, graph)
            print(f"GRAPH={graph}")
    if not result_dirs:
        raise SystemExit(f"No six-method summary.csv files found under {date_root}")
    make_tables(result_dirs, date_root)
    print(f"TABLE={date_root / 'six_method_comparison_summary.md'}")
    print(f"CSV={date_root / 'six_method_comparison_summary.csv'}")
    print(f"LAYER_CSV={date_root / 'six_method_comparison_layer_metrics.csv'}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Overlay exact MET2 floating-window profiles for Lip1/2/3 and min-var."""

import html
import json
import sys
from pathlib import Path


WIDTH, HEIGHT = 1280, 820
LEFT, RIGHT, TOP, BOTTOM = 90, 35, 85, 110
COLORS = {
    "Lip1": "#1f77b4",
    "Lip2": "#d62728",
    "Lip3": "#2ca02c",
    "Min-var": "#9467bd",
}
PATTERNS = ["", "10 4", "3 3", "12 3 3 3", "1 3", "8 3 1 3"]


def main():
    if len(sys.argv) not in (5, 6):
        raise SystemExit(
            f"usage: {sys.argv[0]} lip1.json lip2.json lip3.json "
            "[minvar.json] output.svg"
        )
    labels = ["Lip1", "Lip2", "Lip3"]
    report_paths = sys.argv[1:4]
    output_path = sys.argv[4]
    if len(sys.argv) == 6:
        labels.append("Min-var")
        report_paths.append(sys.argv[4])
        output_path = sys.argv[5]
    reports = {
        label: json.loads(Path(path).read_text())
        for label, path in zip(labels, report_paths)
    }
    points = [
        point
        for report in reports.values()
        for profile in report.get("profiles", [])
        for point in profile.get("points", [])
    ]
    if not points:
        raise SystemExit("input reports contain no density profile points")
    min_x = min(point[0] for point in points)
    max_x = max(point[0] for point in points)
    if min_x == max_x:
        max_x += 1
    min_density = 0.0
    max_density = 1.0
    plot_width = WIDTH - LEFT - RIGHT
    plot_height = HEIGHT - TOP - BOTTOM

    def sx(x):
        return LEFT + (x - min_x) * plot_width / (max_x - min_x)

    def sy(density):
        return TOP + (max_density - density) * plot_height / (
            max_density - min_density
        )

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<text x="640" y="38" text-anchor="middle" font-family="sans-serif" '
        'font-size="24" font-weight="bold">GCD MET2 Floating Density: Lip1/2/3 vs Min-var</text>',
        f'<rect x="{LEFT}" y="{TOP}" width="{plot_width}" height="{plot_height}" '
        'fill="#fafafa" stroke="#333"/>',
    ]
    for tick in range(11):
        density = tick / 10
        y = sy(density)
        lines.extend(
            [
                f'<line x1="{LEFT}" y1="{y:.2f}" x2="{LEFT + plot_width}" '
                f'y2="{y:.2f}" stroke="#ddd"/>',
                f'<text x="{LEFT - 10}" y="{y + 4:.2f}" text-anchor="end" '
                f'font-family="monospace" font-size="12">{density:.1f}</text>',
            ]
        )

    first_report = next(iter(reports.values()))
    for key, label, color in (
        ("min_density_limit", "input min", "#1565c0"),
        ("max_density_limit", "input max", "#c62828"),
    ):
        if key in first_report:
            density = first_report[key]
            y = sy(density)
            lines.append(
                f'<line x1="{LEFT}" y1="{y:.2f}" x2="{LEFT + plot_width}" '
                f'y2="{y:.2f}" stroke="{color}" stroke-dasharray="7 4"/> '
            )
            lines.append(
                f'<text x="{LEFT + plot_width - 8}" y="{y - 5:.2f}" '
                f'text-anchor="end" fill="{color}" font-family="monospace" '
                f'font-size="12">{label}={density:.2f}</text>'
            )

    for lip, report in reports.items():
        profiles = sorted(
            report.get("profiles", []), key=lambda item: item.get("y", 0), reverse=True
        )
        for index, profile in enumerate(profiles):
            dash = PATTERNS[index % len(PATTERNS)]
            dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
            polyline = " ".join(
                f"{sx(x):.2f},{sy(density):.2f}"
                for x, density in profile.get("points", [])
            )
            lines.append(
                f'<polyline points="{polyline}" fill="none" stroke="{COLORS[lip]}" '
                f'stroke-width="0.8" stroke-opacity="0.65"{dash_attr}/>'
            )

    lines.extend(
        [
            f'<text x="{LEFT + plot_width / 2}" y="{HEIGHT - 55}" '
            'text-anchor="middle" font-family="sans-serif" font-size="15">'
            'window left x (DBU)</text>',
            f'<text x="25" y="{TOP + plot_height / 2}" transform="rotate(-90 25 '
            f'{TOP + plot_height / 2})" text-anchor="middle" '
            'font-family="sans-serif" font-size="15">density</text>',
        ]
    )
    legend_y = HEIGHT - 20
    for index, (lip, color) in enumerate(
        (label, COLORS[label]) for label in reports
    ):
        x = LEFT + 35 + index * 170
        lines.append(
            f'<line x1="{x}" y1="{legend_y - 5}" x2="{x + 35}" '
            f'y2="{legend_y - 5}" stroke="{color}" stroke-width="2"/>'
        )
        lines.append(
            f'<text x="{x + 42}" y="{legend_y}" fill="{color}" '
            f'font-family="sans-serif" font-size="14">{html.escape(lip)}</text>'
        )
    lines.append("</svg>")
    Path(output_path).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

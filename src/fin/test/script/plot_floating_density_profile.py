#!/usr/bin/env python3
"""Render exact floating-window density profiles from a JSON report.

Input JSON format:
{
  "min_density_limit": 0.30,
  "max_density_limit": 0.70,
  "profiles": [
    {
      "label": "maximum-window y=2400",
      "y": 2400,
      "points": [[0, 0.32], [500, 0.41], [1000, 0.41]]
    }
  ]
}

Each point is an exact x-event from the ALG2 sweep: [window_left_x, density].
The script uses only the Python standard library and writes a standalone SVG.
"""

import argparse
import html
import json
import math
from pathlib import Path


WIDTH = 1200
HEIGHT = 720
MARGIN_LEFT = 90
MARGIN_RIGHT = 35
MARGIN_TOP = 70
MARGIN_BOTTOM = 90
# The profiles are sorted from high y to low y.  Every ten adjacent y
# profiles share one color; their dash pattern distinguishes them within that
# group without adding an SVG marker for every (potentially thousands of)
# exact x-events.
GROUP_COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd"]
LINE_PATTERNS = [
    "",
    "10 4",
    "3 3",
    "12 3 3 3",
    "1 3",
    "8 3 1 3",
    "6 2",
    "14 4",
    "2 2 8 2",
    "4 2 1 2",
]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="ALG2 profile JSON report")
    parser.add_argument("output", type=Path, help="output SVG file")
    parser.add_argument(
        "--title", default="Floating-window density profile", help="SVG title"
    )
    return parser.parse_args()


def require_points(report):
    profiles = report.get("profiles", [])
    if not profiles:
        raise ValueError("report must contain at least one profile")
    for profile in profiles:
        points = profile.get("points", [])
        if not points:
            raise ValueError("every profile must contain at least one point")
        if any(len(point) != 2 for point in points):
            raise ValueError("each profile point must be [window_left_x, density]")
    return profiles


def make_polyline(points, x_scale, y_scale):
    return " ".join(f"{x_scale(x):.2f},{y_scale(density):.2f}" for x, density in points)


def main():
    args = parse_args()
    report = json.loads(args.input.read_text())
    profiles = sorted(require_points(report), key=lambda profile: profile.get("y", 0), reverse=True)
    all_points = [point for profile in profiles for point in profile["points"]]
    min_x = min(point[0] for point in all_points)
    max_x = max(point[0] for point in all_points)
    if min_x == max_x:
        max_x += 1

    min_density = min(0.0, min(point[1] for point in all_points))
    max_density = max(1.0, max(point[1] for point in all_points))
    for key in ("min_density_limit", "max_density_limit"):
        if key in report:
            min_density = min(min_density, report[key])
            max_density = max(max_density, report[key])
    if min_density == max_density:
        max_density += 1.0

    # A 50-profile report needs a compact multi-column legend; a vertical
    # legend would otherwise run below the SVG viewport.
    legend_columns = 5
    legend_rows = math.ceil(len(profiles) / legend_columns)
    plot_top = MARGIN_TOP + legend_rows * 20 + 15
    plot_width = WIDTH - MARGIN_LEFT - MARGIN_RIGHT
    plot_height = HEIGHT - plot_top - MARGIN_BOTTOM

    def x_scale(value):
        return MARGIN_LEFT + (value - min_x) * plot_width / (max_x - min_x)

    def y_scale(value):
        return plot_top + (max_density - value) * plot_height / (
            max_density - min_density
        )

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{WIDTH / 2}" y="35" text-anchor="middle" '
        f'font-family="sans-serif" font-size="24">{html.escape(args.title)}</text>',
        f'<rect x="{MARGIN_LEFT}" y="{plot_top}" width="{plot_width}" '
        f'height="{plot_height}" fill="#fafafa" stroke="#333"/>',
    ]

    for tick in range(6):
        density = min_density + (max_density - min_density) * tick / 5
        y = y_scale(density)
        lines += [
            f'<line x1="{MARGIN_LEFT}" y1="{y:.2f}" x2="{WIDTH - MARGIN_RIGHT}" '
            f'y2="{y:.2f}" stroke="#ddd"/>',
            f'<text x="{MARGIN_LEFT - 10}" y="{y + 5:.2f}" text-anchor="end" '
            f'font-family="monospace" font-size="14">{density:.3f}</text>',
        ]

    for key, color, label in (
        ("min_density_limit", "#e67e22", "minimum density limit"),
        ("max_density_limit", "#e67e22", "maximum density limit"),
    ):
        if key in report:
            y = y_scale(report[key])
            lines += [
                f'<line x1="{MARGIN_LEFT}" y1="{y:.2f}" x2="{WIDTH - MARGIN_RIGHT}" '
                f'y2="{y:.2f}" stroke="{color}" stroke-dasharray="8 5"/>',
                f'<text x="{WIDTH - MARGIN_RIGHT}" y="{y - 7:.2f}" text-anchor="end" '
                f'font-family="sans-serif" font-size="13" fill="{color}">{label}</text>',
            ]

    for index, profile in enumerate(profiles):
        color = GROUP_COLORS[(index // len(LINE_PATTERNS)) % len(GROUP_COLORS)]
        dash_pattern = LINE_PATTERNS[index % len(LINE_PATTERNS)]
        dash_attribute = f' stroke-dasharray="{dash_pattern}"' if dash_pattern else ""
        points = profile["points"]
        label = profile.get("label", f"y={profile.get('y', 'unknown')}")
        lines.append(
            f'<polyline points="{make_polyline(points, x_scale, y_scale)}" fill="none" '
            f'stroke="{color}" stroke-width="0.75" stroke-opacity="0.85"{dash_attribute}/>'
        )
        legend_column = index // legend_rows
        legend_row = index % legend_rows
        legend_x = MARGIN_LEFT + 10 + legend_column * 210
        legend_y = MARGIN_TOP + 16 + legend_row * 20
        lines += [
            f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 20}" '
            f'y2="{legend_y}" stroke="{color}" stroke-width="1"{dash_attribute}/>',
            f'<text x="{legend_x + 26}" y="{legend_y + 4}" font-family="sans-serif" '
            f'font-size="11">{html.escape(label)}</text>',
        ]

    lines += [
        f'<text x="{WIDTH / 2}" y="{HEIGHT - 25}" text-anchor="middle" '
        'font-family="sans-serif" font-size="16">window left x (DBU)</text>',
        f'<text x="25" y="{HEIGHT / 2}" text-anchor="middle" font-family="sans-serif" '
        f'font-size="16" transform="rotate(-90 25 {HEIGHT / 2})">density</text>',
        "</svg>",
    ]
    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Create one four-panel SVG comparing met2 floating-density profiles."""

import html
import json
import sys
from pathlib import Path


PANEL_W, PANEL_H = 900, 520
PADDING_X, PADDING_Y = 70, 65
COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd"]
PATTERNS = ["", "10 4", "3 3", "12 3 3 3", "1 3", "8 3 1 3", "6 2", "14 4", "2 2 8 2", "4 2 1 2"]


def draw_panel(lines, report, title, column, row, min_x, max_x, min_d, max_d):
    left = column * PANEL_W + PADDING_X
    top = row * PANEL_H + 45
    width, height = PANEL_W - 100, PANEL_H - 105
    profiles = sorted(report["profiles"], key=lambda p: p.get("y", 0), reverse=True)
    sx = lambda x: left + (x - min_x) * width / (max_x - min_x)
    sy = lambda d: top + (max_d - d) * height / (max_d - min_d)
    lines += [
        f'<text x="{left + width / 2}" y="{top - 17}" text-anchor="middle" font-family="sans-serif" font-size="20">{html.escape(title)}</text>',
        f'<rect x="{left}" y="{top}" width="{width}" height="{height}" fill="#fafafa" stroke="#333"/>',
    ]
    for tick in range(6):
        density = min_d + (max_d - min_d) * tick / 5
        y = sy(density)
        lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + width}" y2="{y:.2f}" stroke="#ddd"/>')
        lines.append(f'<text x="{left - 8}" y="{y + 4:.2f}" text-anchor="end" font-family="monospace" font-size="11">{density:.2f}</text>')
    for label, density, color in (("input min limit", report["min_density_limit"], "#1565c0"),
                                  ("input max limit", report["max_density_limit"], "#c62828")):
        y = sy(density)
        lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + width}" y2="{y:.2f}" stroke="{color}" stroke-width="1.4" stroke-dasharray="6 3"/>')
        lines.append(f'<text x="{left + width - 5}" y="{y - 5:.2f}" text-anchor="end" font-family="monospace" font-size="12" fill="{color}">{label}={density:.6f}</text>')
    for i, profile in enumerate(profiles):
        color = COLORS[(i // 10) % len(COLORS)]
        pattern = PATTERNS[i % len(PATTERNS)]
        dash = f' stroke-dasharray="{pattern}"' if pattern else ""
        points = " ".join(f"{sx(x):.2f},{sy(d):.2f}" for x, d in profile["points"])
        lines.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="0.75" stroke-opacity="0.85"{dash}/>')
    lines.append(f'<text x="{left + width / 2}" y="{top + height + 32}" text-anchor="middle" font-family="sans-serif" font-size="13">window left x (DBU)</text>')


def main():
    if len(sys.argv) != 6:
        raise SystemExit(f"usage: {sys.argv[0]} prefill.json density.json lp.json min_amount.json output.svg")
    titles = ["Pre-fill", "OpenROAD", "LP Var fill", "LP min-amount fill"]
    reports = [json.loads(Path(path).read_text()) for path in sys.argv[1:5]]
    all_points = [p for report in reports for profile in report["profiles"] for p in profile["points"]]
    min_x, max_x = min(p[0] for p in all_points), max(p[0] for p in all_points)
    min_d, max_d = min(0.0, min(p[1] for p in all_points)), max(1.0, max(p[1] for p in all_points))
    svg_height = PANEL_H * 2 + 80
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {PANEL_W * 2} {svg_height}">', '<rect width="100%" height="100%" fill="white"/>']
    for index, (report, title) in enumerate(zip(reports, titles)):
        draw_panel(lines, report, title, index % 2, index // 2, min_x, max_x, min_d, max_d)
    legend_y = PANEL_H * 2 + 30
    lines.append(f'<text x="{PADDING_X}" y="{legend_y}" font-family="sans-serif" font-size="15">y-profile groups (descending y, DBU):</text>')
    y_values = [profile.get("y", 0) for profile in sorted(reports[0]["profiles"], key=lambda p: p.get("y", 0), reverse=True)]
    for group, color in enumerate(COLORS):
        x = PADDING_X + 250 + group * 275
        group_y = y_values[group * 10:(group + 1) * 10]
        label = f"{group_y[0]}-{group_y[-1]}" if group_y else "n/a"
        lines.append(f'<line x1="{x}" y1="{legend_y - 5}" x2="{x + 28}" y2="{legend_y - 5}" stroke="{color}" stroke-width="2"/>')
        lines.append(f'<text x="{x + 35}" y="{legend_y}" font-family="sans-serif" font-size="12">{label}</text>')
    lines.append(f'<text x="{PADDING_X}" y="{legend_y + 28}" font-family="sans-serif" font-size="13">Blue/red dashed lines show the input min/max density limits. Dash patterns distinguish individual profiles within a color group.</text>')
    lines.append("</svg>")
    Path(sys.argv[5]).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

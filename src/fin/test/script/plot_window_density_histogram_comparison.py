#!/usr/bin/env python3
"""Render comparable FIN window-density histograms from several reports."""

import json
import sys
from pathlib import Path
from xml.sax.saxutils import escape


def format_area(area: float) -> str:
    if area >= 1e9:
        return f"{area / 1e9:.2f}B DBU²"
    if area >= 1e6:
        return f"{area / 1e6:.2f}M DBU²"
    return f"{area:.0f} DBU²"


def main() -> None:
    if len(sys.argv) < 4:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} OUTPUT.svg [LABEL=]REPORT.json [REPORT.json ...]")

    output_path = Path(sys.argv[1])
    reports = []
    for argument in sys.argv[2:]:
        label, separator, filename = argument.partition("=")
        if not separator:
            filename = argument
            label = ""
        path = Path(filename)
        reports.append((label, path, json.loads(path.read_text())))
    layers = [layer["layer"] for layer in reports[0][2]["layers"]]
    if any([layer["layer"] for layer in report["layers"]] != layers
           for _, _, report in reports[1:]):
        raise ValueError("reports use incompatible layer sets")

    panel_width, panel_height = 330, 155
    left, top, right, bottom = 105, 88, 28, 42
    gap_x, gap_y = 20, 32
    width = left + len(reports) * panel_width + (len(reports) - 1) * gap_x + right
    height = top + len(layers) * panel_height + (len(layers) - 1) * gap_y + bottom
    colors = ("#1565c0", "#ef6c00", "#2e7d32", "#7b1fa2", "#c62828")
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#fafafa"/>',
        '<text x="24" y="30" font-family="sans-serif" font-size="21" font-weight="bold">Window-density histogram comparison</text>',
        '<text x="24" y="52" font-family="sans-serif" font-size="13" fill="#444">Green = requested minimum; red = requested maximum. Bar scales are shared within each layer row.</text>',
    ]
    for column, (label, path, report) in enumerate(reports):
        minimum = report["min_window_density_limit"]
        maximum = report.get("max_density_limit", report["max_window_density_limit"])
        x = left + column * (panel_width + gap_x)
        title = label or f"{minimum:.0%}–{maximum:.0%}"
        subtitle = f"{minimum:.0%}–{maximum:.0%}" if label else path.parent.name
        parts.append(f'<text x="{x + panel_width / 2:.1f}" y="{top - 22}" font-family="sans-serif" font-size="17" font-weight="bold" text-anchor="middle">{escape(title)}</text>')
        parts.append(f'<text x="{x + panel_width / 2:.1f}" y="{top - 5}" font-family="sans-serif" font-size="11" fill="#555" text-anchor="middle">{escape(subtitle)}</text>')

    for row, layer_name in enumerate(layers):
        row_layers = [report["layers"][row] for _, _, report in reports]
        row_max = max(max(layer["window_density_histogram"]["counts"]) for layer in row_layers) or 1
        y = top + row * (panel_height + gap_y)
        parts.append(f'<text x="{left - 14}" y="{y + panel_height / 2 - 7:.1f}" font-family="sans-serif" font-size="17" font-weight="bold" text-anchor="end">{escape(layer_name)}</text>')
        parts.append(f'<text x="{left - 14}" y="{y + panel_height / 2 + 11:.1f}" font-family="sans-serif" font-size="11" fill="#555" text-anchor="end">0–{row_max} windows</text>')
        for column, (_, _, report) in enumerate(reports):
            layer = report["layers"][row]
            histogram = layer["window_density_histogram"]
            counts = histogram["counts"]
            minimum = report["min_window_density_limit"]
            maximum = report.get("max_density_limit", report["max_window_density_limit"])
            x = left + column * (panel_width + gap_x)
            bar_width = panel_width / len(counts)
            parts.extend((
                f'<rect x="{x}" y="{y}" width="{panel_width}" height="{panel_height}" fill="white" stroke="#d0d0d0"/>',
                f'<line x1="{x}" y1="{y + panel_height}" x2="{x + panel_width}" y2="{y + panel_height}" stroke="#333"/>',
            ))
            for index, count in enumerate(counts):
                bar_height = panel_height * count / row_max
                bar_x = x + index * bar_width + 1
                parts.append(f'<rect x="{bar_x:.2f}" y="{y + panel_height - bar_height:.2f}" width="{bar_width - 2:.2f}" height="{bar_height:.2f}" fill="{colors[row % len(colors)]}"/>')
            min_x = x + panel_width * minimum
            max_x = x + panel_width * maximum
            placed_area = layer.get("placed_fill_area")
            placed_area_label = (
                f"final fill: {format_area(placed_area)}"
                if placed_area is not None and layer["solved"]
                else ""
            )
            parts.extend((
                f'<line x1="{min_x:.2f}" y1="{y}" x2="{min_x:.2f}" y2="{y + panel_height}" stroke="#2e7d32" stroke-width="2" stroke-dasharray="5 4"/>',
                f'<line x1="{max_x:.2f}" y1="{y}" x2="{max_x:.2f}" y2="{y + panel_height}" stroke="#c62828" stroke-width="2" stroke-dasharray="5 4"/>',
                f'<text x="{x + 4}" y="{y + 15}" font-family="sans-serif" font-size="11" fill="{"#1b5e20" if layer["solved"] else "#b71c1c"}">{"SOLVED" if layer["solved"] else "INFEASIBLE"}</text>',
                f'<text x="{x + panel_width - 4}" y="{y + 15}" font-family="sans-serif" font-size="11" fill="#333" text-anchor="end">{escape(placed_area_label)}</text>',
                f'<text x="{x}" y="{y + panel_height + 15}" font-family="sans-serif" font-size="10">0.0</text>',
                f'<text x="{x + panel_width}" y="{y + panel_height + 15}" font-family="sans-serif" font-size="10" text-anchor="end">1.0</text>',
            ))
    parts.append("</svg>")
    output_path.write_text("\n".join(parts) + "\n")


if __name__ == "__main__":
    main()

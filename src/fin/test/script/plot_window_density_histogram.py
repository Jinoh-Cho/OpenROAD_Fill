#!/usr/bin/env python3
"""Render the window-density histogram in a FIN density report as SVG."""

import json
import sys
from pathlib import Path
from xml.sax.saxutils import escape


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} REPORT.json OUTPUT.svg")

    report_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    report = json.loads(report_path.read_text())
    layers = report["layers"]
    minimum_limit = report["min_window_density_limit"]
    maximum_limit = report.get("max_density_limit")
    if maximum_limit is None:
        maximum_limit = report["max_window_density_limit"]
    histograms = [layer["window_density_histogram"] for layer in layers]
    bin_count = len(histograms[0]["counts"])
    if any(len(histogram["counts"]) != bin_count for histogram in histograms):
        raise ValueError("layers use incompatible histogram bin counts")

    width, height = 1100, 220 * len(layers) + 80
    left, right, top, chart_height = 75, 30, 48, 155
    chart_width = width - left - right
    colors = ("#1565c0", "#ef6c00", "#2e7d32", "#7b1fa2", "#c62828")
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<text x="20" y="28" font-family="sans-serif" font-size="20">Sliding-window metal-density histogram</text>',
    ]
    for layer_index, (layer, histogram) in enumerate(zip(layers, histograms)):
        y0 = top + layer_index * 220
        counts = histogram["counts"]
        maximum = max(counts) or 1
        bar_width = chart_width / bin_count
        parts.extend((
            f'<text x="{left}" y="{y0 - 12}" font-family="sans-serif" font-size="16">{escape(layer["layer"])} ({layer["window_count"]} windows)</text>',
            f'<line x1="{left}" y1="{y0 + chart_height}" x2="{width - right}" y2="{y0 + chart_height}" stroke="black"/>',
            f'<line x1="{left}" y1="{y0}" x2="{left}" y2="{y0 + chart_height}" stroke="black"/>',
            f'<text x="8" y="{y0 + 8}" font-family="sans-serif" font-size="12">{maximum}</text>',
            f'<text x="20" y="{y0 + chart_height + 4}" font-family="sans-serif" font-size="12">0</text>',
        ))
        for bin_index, count in enumerate(counts):
            bar_height = chart_height * count / maximum
            x = left + bin_index * bar_width + 1
            y = y0 + chart_height - bar_height
            parts.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_width - 2:.2f}" height="{bar_height:.2f}" fill="{colors[layer_index % len(colors)]}"/>')
            if bin_index % 2 == 0:
                density = bin_index * histogram["bin_width"]
                parts.append(f'<text x="{x:.2f}" y="{y0 + chart_height + 17}" font-family="sans-serif" font-size="11">{density:.1f}</text>')
        min_x = left + chart_width * minimum_limit
        max_x = left + chart_width * maximum_limit
        parts.extend((
            f'<line x1="{min_x:.2f}" y1="{y0}" x2="{min_x:.2f}" y2="{y0 + chart_height}" stroke="#2e7d32" stroke-width="2" stroke-dasharray="5 4"/>',
            f'<text x="{min_x:.2f}" y="{y0 + 16}" fill="#2e7d32" font-family="sans-serif" font-size="12" text-anchor="middle">min limit={minimum_limit:.4f}</text>',
            f'<line x1="{max_x:.2f}" y1="{y0}" x2="{max_x:.2f}" y2="{y0 + chart_height}" stroke="#c62828" stroke-width="2" stroke-dasharray="5 4"/>',
            f'<text x="{max_x:.2f}" y="{y0 + 32}" fill="#c62828" font-family="sans-serif" font-size="12" text-anchor="middle">max limit={maximum_limit:.4f}</text>',
        ))
        parts.append(f'<text x="{width - right - 15}" y="{y0 + chart_height + 17}" font-family="sans-serif" font-size="11">1.0</text>')
    parts.append('</svg>')
    output_path.write_text("\n".join(parts) + "\n")


if __name__ == "__main__":
    main()

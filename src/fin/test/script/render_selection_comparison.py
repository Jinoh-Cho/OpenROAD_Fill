#!/usr/bin/env python3
"""Render CSVs exported by SelectionPolicyTest.VerifyExistingSmallOdb.

Use FIN_SELECTION_TEST_ODB and FIN_SELECTION_EXPORT_DIR when running that test.
Both policies share one PDLP minimum-fill LP solution. This renderer consumes
actual candidate/selected rectangles; it does not synthesize or change fills.
"""
import argparse
import json
import os
from pathlib import Path

import numpy as np


def load_csv(path):
    return np.loadtxt(str(path), delimiter=",", skiprows=1, ndmin=2)


def crop_rects(rects, box):
    if not len(rects):
        return rects
    x1, y1, x2, y2 = box
    return rects[(rects[:, 0] < x2) & (rects[:, 2] > x1)
                 & (rects[:, 1] < y2) & (rects[:, 3] > y1)]


def add_rectangles(ax, rects, color, dbu, box):
    from matplotlib.collections import PolyCollection
    data = crop_rects(rects, box) / dbu
    if not len(data):
        return
    points = np.stack((data[:, [0, 1]], data[:, [2, 1]],
                       data[:, [2, 3]], data[:, [0, 3]]), axis=1)
    ax.add_collection(PolyCollection(points, facecolors=color, edgecolors="none"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--tile", type=int)
    args = parser.parse_args()
    root = args.directory.resolve()
    output = root / "images"
    output.mkdir(exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(output / ".mplconfig"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    from PIL import Image, ImageDraw

    meta = json.loads((root / "metadata.json").read_text())
    dbu = meta["dbu"]
    tiles = load_csv(root / "tiles.csv")
    candidates = load_csv(root / "candidates.csv")
    metal = load_csv(root / "metal.csv")
    fills = {name: load_csv(root / (name + ".csv")) for name in ("greedy", "uniform")}
    colors = {"greedy": "#2878B5", "uniform": "#E88323"}
    box = [tiles[:, 1].min(), tiles[:, 2].min(),
           tiles[:, 3].max(), tiles[:, 4].max()]
    tile_areas = (tiles[:, 3] - tiles[:, 1]) * (tiles[:, 4] - tiles[:, 2])
    if args.tile is None:
        # Choose by shared input capacity, without looking at either output.
        full = np.where(tile_areas == tile_areas.max())[0]
        tile_index = int(full[np.argmax(tiles[full, 7])])
    else:
        tile_index = int(np.where(tiles[:, 0] == args.tile)[0][0])
    tile_id = int(tiles[tile_index, 0])
    tile_box = tiles[tile_index, 1:5].tolist()
    tile_candidates = candidates[candidates[:, 0] == tile_id, 1:5]
    centers = (tile_candidates[:, :2] + tile_candidates[:, 2:]) / 2
    # Detail crop: most candidate-rich of the shared tile's 4x4 cells.
    weights = ((tile_candidates[:, 2] - tile_candidates[:, 0])
               * (tile_candidates[:, 3] - tile_candidates[:, 1]))
    hist, x_edges, y_edges = np.histogram2d(
        centers[:, 0], centers[:, 1], bins=4,
        range=[[tile_box[0], tile_box[2]], [tile_box[1], tile_box[3]]], weights=weights)
    ix, iy = np.unravel_index(np.argmax(hist), hist.shape)
    detail_box = [x_edges[ix], y_edges[iy], x_edges[ix + 1], y_edges[iy + 1]]

    metrics = {}
    for name in fills:
        actual = load_csv(root / (name + "_tiles.csv"))
        windows = load_csv(root / (name + "_windows.csv"))
        timing = json.loads((root / (name + "_timing.json")).read_text())
        deficit = actual[:, 1] - tiles[:, 6]
        metrics[name] = dict(timing,
            area_um2=float(actual[:, 1].sum() / dbu ** 2),
            target_error_um2=float(deficit.sum() / dbu ** 2),
            max_tile_shortage_um2=float(max(0, -deficit.min()) / dbu ** 2),
            max_tile_excess_um2=float(max(0, deficit.max()) / dbu ** 2),
            min_window_density=float(windows[:, 1].min()),
            max_window_density=float(windows[:, 1].max()),
            windows_below_30_percent=int((windows[:, 1] < .3).sum()),
            windows_above_60_percent=int((windows[:, 1] > .6).sum()))
    context = dict(meta, tile=tile_id, tile_box_dbu=tile_box,
                   detail_box_dbu=list(map(float, detail_box)),
                   tile_choice="largest candidate capacity among full tiles",
                   detail_choice="largest shared candidate capacity among 4x4 tile cells",
                   policies=metrics)
    (root / "comparison_metrics.json").write_text(json.dumps(context, indent=2) + "\n")

    def setup(ax, bounds, title):
        ax.set_xlim(bounds[0] / dbu, bounds[2] / dbu)
        ax.set_ylim(bounds[1] / dbu, bounds[3] / dbu)
        ax.set_aspect("equal")
        ax.set_title(title, fontsize=12)
        ax.set_xlabel("x (um)")
        ax.set_ylabel("y (um)")

    def plot_pair(filename, bounds, overview=False):
        fig, axes = plt.subplots(1, 2, figsize=(15, 6), constrained_layout=True)
        if overview:
            width = 2600
            scale = width / (box[2] - box[0])
            height = int(np.ceil((box[3] - box[1]) * scale))
            base = Image.new("RGB", (width, height), "white")
            draw = ImageDraw.Draw(base)
            def draw_rects(draw, data, color):
                for x1, y1, x2, y2 in data:
                    draw.rectangle((int((x1-box[0])*scale), int((box[3]-y2)*scale),
                                    int((x2-box[0])*scale), int((box[3]-y1)*scale)), fill=color)
            draw_rects(draw, metal, "#A8AFB7")
        for ax, name in zip(axes, fills):
            setup(ax, bounds, name.upper())
            if overview:
                picture = base.copy()
                draw_rects(ImageDraw.Draw(picture), fills[name], colors[name])
                picture.save(str(output / (name + "_overview.png")))
                ax.imshow(picture, extent=np.asarray(box)[[0, 2, 1, 3]] / dbu,
                          origin="upper", interpolation="nearest")
                x1, y1, x2, y2 = np.asarray(tile_box) / dbu
                ax.plot([x1,x2,x2,x1,x1], [y1,y1,y2,y2,y1], color="#B100B1", lw=1)
            else:
                add_rectangles(ax, metal, "#A8AFB7", dbu, bounds)
                add_rectangles(ax, tile_candidates, "#F0F3F6", dbu, bounds)
                add_rectangles(ax, fills[name], colors[name], dbu, bounds)
            ax.legend(handles=[Patch(color="#A8AFB7", label="Existing M2"),
                               Patch(color=colors[name], label="Selected fill")], fontsize=9)
        fig.suptitle("TPU 2x1 / M2 / same candidates + same PDLP Min Amount targets\n"
                     + ("Overview; magenta box marks comparison tile" if overview
                        else "Tile %d: %s" % (tile_id, "detail" if filename.startswith("detail") else "full tile")),
                     fontsize=13)
        fig.savefig(str(output / filename), dpi=200, bbox_inches="tight", pad_inches=0.2)
        plt.close(fig)

    plot_pair("comparison_overview.png", box, True)
    plot_pair("comparison_tile.png", tile_box)
    plot_pair("detail_comparison.png", detail_box)

    def rect_set(name):
        return {tuple(map(int, r)) for r in crop_rects(fills[name], detail_box)}
    a, b = rect_set("greedy"), rect_set("uniform")
    fig, ax = plt.subplots(figsize=(8, 7), constrained_layout=True)
    setup(ax, detail_box, "Selected rectangle differences (same crop)")
    add_rectangles(ax, metal, "#D1D5DA", dbu, detail_box)
    groups = [(a & b, "#2878B5", "Both"),
              (a - b, "#BA3B92", "Greedy only"),
              (b - a, "#E88323", "Uniform only")]
    for rects, color, label in groups:
        if rects:
            add_rectangles(ax, np.asarray(list(rects)), color, dbu, detail_box)
    ax.legend(handles=[Patch(color=c, label=l) for _, c, l in groups], fontsize=10)
    fig.savefig(str(output / "detail_difference.png"), dpi=200, bbox_inches="tight", pad_inches=0.2)
    plt.close(fig)

    lines = ["# TPU 2x1 selection comparison", "",
             "M2; one PDLP Min Amount solution shared by both policies. Placement uses 4 threads.",
             "Candidate sizes 0.16/0.08/0.04 um; metal/fill spacing 0.018 um; window 50 um, resolution 4.",
             "Tile/window lower density 0.2/0.3, upper density 1.0/0.6; no density-bound adjustment.",
             "", "| Metric | Greedy | Uniform |", "|---|---:|---:|"]
    for key, label, fmt in [("fills", "Selected rectangles", ",d"),
                            ("area_um2", "Placed area (um^2)", ".6f"),
                            ("target_error_um2", "Placed minus LP target (um^2)", ".6f"),
                            ("select_seconds", "Selection wall time, single measurement (s)", ".6f"),
                            ("min_window_density", "Minimum window density", ".9f"),
                            ("max_window_density", "Maximum window density", ".9f"),
                            ("windows_below_30_percent", "Windows below requested 30%", "d"),
                            ("windows_above_60_percent", "Windows above requested 60%", "d")]:
        lines.append("| %s | %s | %s |" % (label, format(metrics['greedy'][key],fmt), format(metrics['uniform'][key],fmt)))
    lines += ["", "Both paths passed 1T/4T exact selection equality and full geometric tile/window density verification.",
              "Uniform adapts the interior/parity policy to OpenROAD's tile-local multi-size candidates; it is not a bit-exact BlazeDFM port.",
              "The policy changes rounding as well as position, so selected counts and areas need not match.",
              "This is direct generation/LP/selection validation, not a full OpenROAD command runtime benchmark.",
              "No global uniformity improvement is established by these images alone.",
              "", "Tile %d was chosen from shared input by candidate capacity, not by policy output." % tile_id,
              "The detail crop is the candidate-richest 4x4 cell of that tile.",
              "Zoom images use vector rectangles at true scale; overview is rasterized and subpixel shapes can appear thicker."]
    (root / "comparison.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("Images: " + str(output))


if __name__ == "__main__":
    main()

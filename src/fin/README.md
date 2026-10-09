# Metal fill

This module inserts floating metal fill shapes to meet metal density
design rules while obeying DRC constraints. It is driven by a `json`
configuration file.

## Commands

```{note}
- Parameters in square brackets `[-param param]` are optional.
- Parameters without square brackets `-param2 param2` are required.
```

### Density Fill

This command performs density fill to meet metal density DRC rules.

```tcl
density_fill
    [-rules rules_file]
    [-area {lx ly ux uy}]
```

#### Options

| Switch Name | Description | 
| ----- | ----- |
| `-rules` | Specify `json` rule file. |
| `-area` | Optional. If not specified, the core area will be used. |

### Tile-grid metal area

This experimental command partitions the selected area into tiles and creates
only full `window × window` density windows. Windows slide by one tile in each
direction. `resolution` specifies the number of tiles along one window edge,
so each window contains `resolution × resolution` tiles. The command reports
per-layer total metal area and the minimum/maximum sliding-window density.
It can also write one layout SVG per configured layer.

```tcl
tile_grid_metal_area
    [-rules rules_file]
    [-area {lx ly ux uy}]
    -window window_size
    [-origin {x y}]
    [-resolution resolution]
    [-svg file]
```

### Fixed-dissection LP fill placement

`fixed_dissection_lp_min_var_fill` first generates legal non-OPC fill candidates in
each tile using the JSON shape and spacing rules. It uses the sum of those
candidates as the LP capacity, then inserts a subset whose area does not
exceed the LP solution for that tile. Candidates are inset from tile
boundaries by the fill spacing, so independently selected candidates in
neighboring tiles remain legal. This initial implementation deliberately does
not run a post-placement repair pass and does not yet place OPC fill.

```tcl
fixed_dissection_lp_min_var_fill
    -rules rules_file
    [-area {lx ly ux uy}]
    -window window_size
    [-origin {x y}]
    [-resolution resolution]
    [-min_tile_density density]
    [-max_tile_density density]
    [-min_window_density density]
    [-max_window_density density]
    [-svg file]
```

`-min_tile_density` and `-max_tile_density` are hard post-fill density bounds
for every tile; `-max_tile_density` defaults to `1.0`.
`-min_window_density` is an optional hard minimum post-fill density for every
sliding window. `-max_window_density` remains the maximum density for every sliding
window and defaults to `1.0`. If a tile's legal capacity, the sliding-window density bounds, or the
discrete fill candidates cannot satisfy the constraints together, the command
reports
an infeasible error and does not retain any fill created by that command.

The return value is the total area actually placed, in DBU². If `-svg` is
given, FIN writes two SVGs per configured layer: `<file>_<layer>_fillable.svg`
shows the pre-placement fillable regions in green, and `<file>_<layer>.svg`
shows those regions plus the selected fill rectangles in blue.

### Minimum-fill-amount LP placement

`fixed_dissection_lp_min_amount_fill` uses the same candidate generation and
placement path as `fixed_dissection_lp_min_var_fill`, but minimizes total planned fill
area while satisfying the density bounds.

```tcl
fixed_dissection_lp_min_amount_fill
    -rules rules_file
    [-area {lx ly ux uy}]
    -window window_size
    [-origin {x y}]
    [-resolution resolution]
    [-min_tile_density density]
    [-max_tile_density density]
    [-min_window_density density]
    [-max_window_density density]
    [-svg file]
    [-density_report file]
```

The return value is the area actually placed, in DBU². Output options and
defaults are shared with `fixed_dissection_lp_min_var_fill`.

### Lip LP placement

`fixed_dissection_lp_lip_fill` selects the Lip1, Lip2, or Lip3 neighborhood
objective with `-lip_type`. It shares the legal candidates, density bounds,
and physical placement path of the other LP fill commands.

```tcl
fixed_dissection_lp_lip_fill
    -rules rules_file
    [-area {lx ly ux uy}]
    -window window_size
    -lip_type {1|2|3}
    [-origin {x y}]
    [-resolution resolution]
    [-min_tile_density density]
    [-max_tile_density density]
    [-min_window_density density]
    [-max_window_density density]
    [-svg file]
    [-density_report file]
```

The return value is the area actually placed, in DBU². Output options and
defaults are shared with `fixed_dissection_lp_min_var_fill`.

## Source architecture

The public FIN commands are limited to density fill, MinVar LP fill,
minimum-fill-amount LP fill, Lip1/2/3 LP fill, and post-fill density analysis.
The old `fixed_dissection_lp_fill` command has been renamed to
`fixed_dissection_lp_min_var_fill`; no compatibility alias is provided.

| Files | Responsibility |
| --- | --- |
| `include/fin/Finale.h`, `src/Finale.cpp` | Public facade selecting density fill or a named LP objective. |
| `src/finale.tcl`, `src/finale.i`, `src/finale-py.i` | Tcl option validation, shared unit conversion, and language bindings. |
| `src/DensityFill.h/.cpp` | Original OpenROAD rule-based physical fill placement. |
| `src/LPFill.h/.cpp` | Candidate generation, LP dispatch, physical placement, and post-fill analysis orchestration. |
| `src/LPFillUtil.h/.cpp` | Spacing, legal tile regions, and rectangular candidate generation. |
| `src/FillConfig.h/.cpp`, `src/FillGeometry.h/.cpp` | Shared JSON rules and existing-layout geometry collection. |
| `src/TileGrid.h/.cpp` | Tile/window geometry and window-to-tile indices. |
| `src/DensityAnalysis.h`, `src/DensityAnalyzer.h/.cpp` | Shared result types, tile/window densities, ALG2/ALG3, statistics, and histograms. |
| `src/FixedDissectionLp.h` | Shared LP problem and result types. |
| `src/MinVarLP.h/.cpp`, `src/MinFillAmountLp.h/.cpp`, `src/LipLpFill.h/.cpp`, `src/FillLpSolver.h/.cpp` | Named objectives and numerical solvers. |
| `src/FillReporter.h/.cpp`, `src/FillSvgWriter.h/.cpp` | JSON results/profiles and SVG rendering. |
| `src/MakeFinale.cpp`, `include/fin/MakeFinale.h` | FIN Tcl package initialization. |

MinVar maximizes the minimum post-fill window metal area; it does not directly
minimize statistical variance. All LP placement commands share the same
candidate and placement path. The comparison scripts
`test/script/gcd_fill_method_comparison.tcl` and
`test/script/gcd_lip_lp_comparison.tcl` use these commands and analyze actual
placed geometry with `tile_grid_metal_area`.

## Six-method GCD and IBEX comparisons

From the repository root, run:

```shell
python3 src/fin/test/script/run_gcd_six_method_comparison.py
python3 src/fin/test/script/run_ibex_six_method_comparison.py
```

The driver launches six independent OpenROAD processes, each reading the same
GCD or IBEX prefill DEF: Density, MinVar, MinFillAmount, Lip1, Lip2, and Lip3. Use a
binary rebuilt with the current FIN commands. Defaults are a 50-micron window,
resolution 2, minimum tile density 0.20, and window density bounds 0.30–0.60.
Density uses its original JSON-rule-based algorithm, not the LP density bounds;
all methods are evaluated with the same post-fill analysis bounds.

Options include `--openroad`, `--rules`, `--output`, `--resolution`, `--window`,
the four `--min/max-tile/window-density` options, `--algorithms alg3`, `--svg`,
and `--dry-run`. Output directories must be new to avoid overwriting results.

Results are saved under `test/results/<date>/gcd/six_method_comparison_<time>/`.
The IBEX driver uses `test/results/<date>/ibex/six_method_comparison_<time>/`.
`summary.csv` contains per-layer grid density statistics and exact floating
density extrema; `summary.json` records method status and timings. Area and
timing columns are method totals repeated on each layer row. Density has no
LP-returned placed area, so that column is empty; `added_metal_area_dbu2`
measures the common post-minus-pre metal union area for every method. Each
method directory contains `run.log`, before/after density JSON, exact profiles,
and the LP report when applicable. Failures are logged without skipping later
methods; the driver exits nonzero if any method fails.

## Example scripts

The rules `json` file controls fill and you can see an example
[here](https://github.com/The-OpenROAD-Project/OpenROAD-flow-scripts/blob/master/flow/platforms/sky130hd/fill.json).

The schema for the `json` is:

```json
{
  "layers": {
    "<group_name>": {
      "layers": "<list of integer gds layers>",
      "names": "<list of name strings>",
      "opc": {
        "datatype":  "<list of integer gds datatypes>",
        "width":   "<list of widths in microns>",
        "height":   "<list of heightsin microns>",
        "space_to_fill": "<real: spacing between fills in microns>",
        "space_to_non_fill": "<real: spacing to non-fill shapes in microns>",
        "space_line_end": "<real: spacing to end of line in microns>"
      },
      "non-opc": {
        "datatype":  "<list of integer gds datatypes>",
        "width":   "<list of widths in microns>",
        "height":   "<list of heightsin microns>",
        "space_to_fill": "<real: spacing between fills in microns>",
        "space_to_non_fill": "<real: spacing to non-fill shapes in microns>"
      }
    }, ...
  }
}
```

The `opc` section is optional depending on your process.

The width/height lists are effectively parallel arrays of shapes to try
in left to right order (generally larger to smaller).

The layer grouping is for convenience. For example in some technologies many
layers have similar rules so it is convenient to have a `Mx`, `Cx` group.

This all started out in `klayout` so there are some obsolete fields that the
parser accepts but ignores (e.g., `space_to_outline`).

## Regression tests

There are a set of regression tests in `./test`. For more information, refer to this [section](../../README.md#regression-tests). 

Simply run the following script: 

```shell
./test/regression
```

## Limitations

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

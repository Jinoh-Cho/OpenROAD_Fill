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

### LP Fill

Optimize metal fill using a continuous linear program and place whole fill
rectangles from the same JSON rule format used by `density_fill`.

```tcl
lp_fill
    -rules rules_file
    -objective min_fill|min_var
    -window_size {width height}
    -window_step {x_step y_step}
    [-solver glop|pdlp]
    [-solver_threads threads]
    [-num_threads threads]
    [-min_density density]
    [-max_density density]
    [-min_tile_density density]
    [-max_tile_density density]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-rules` | Required JSON fill rule file. LP fill currently supports non-OPC rules only. |
| `-objective` | Required. `min_fill` minimizes total fill area; `min_var` maximizes the minimum window density (the J40 max-min LP), rather than minimizing statistical variance. |
| `-window_size` | Required positive window width and height, in microns. Windows must fit inside the core. |
| `-window_step` | Required positive horizontal and vertical distance between window starts, in microns. Windows start at the core lower-left corner; only fully contained windows are evaluated. |
| `-solver` | `glop` (default) or `pdlp`, both open-source OR-Tools LP solvers. |
| `-solver_threads` | Positive solver thread count; default `1`. PDLP supports multiple threads; GLOP uses one and warns if another count is requested. |
| `-num_threads` | Positive thread count for tile geometry and selection; default `1`. Database insertion is serial. |
| `-min_density` | Requested minimum post-fill window density, from `0` to `1`; default `0.20`. |
| `-max_density` | Maximum post-fill window density, from `0` to `1` and at least the minimum; default `0.70`. |
| `-min_tile_density` | Requested minimum post-fill tile density, from `0` to `1`; default `0` (no tile minimum). Includes existing metal. |
| `-max_tile_density` | Maximum post-fill tile density, from `0` to `1` and at least the tile minimum; default `1` (no additional tile limit). Includes existing metal. |

Placement always selects candidates sequentially in generation order, without
exceeding the LP area budget. There is no placement strategy selection option.

Window boundaries partition the core into independent rectangular tiles. Fill
candidates remain inside their tiles, with conservative spacing at tile edges.
The available candidate packing may differ from `density_fill` even under the
same rules. Existing fill is included in density and spacing calculations.
Fill obstructions block placement but do not contribute metal density.

Tile bounds apply to each partition tile independently of the window bounds.
A tile minimum exceeding its candidate capacity is relaxed to that capacity,
and the number of relaxed tile bounds is reported. A tile already above a
specified maximum receives no additional fill; existing metal is retained.
A tile maximum below `1` can conflict with window minimum constraints and make
the LP infeasible. The default tile bounds preserve the original behavior.
When tile bounds are enabled, actual post-fill tile density extrema and counts
of tiles below/above the requested bounds are logged separately from windows.
Whole-shape selection can leave a tile below its continuous LP minimum.

A window whose requested minimum exceeds its candidate capacity requires all
available candidate area instead. The command reports how many lower bounds
were relaxed. `set_debug_level FIN lp_fill 1` additionally reports each relaxed
window, its effective lower bound, and its available candidate area.
Existing metal exceeding an upper bound cannot be removed, so
that upper bound is omitted. Other window upper bounds remain active; conflicts
between overlapping windows can still make the LP infeasible. A solver failure
returns a Tcl error and removes all fill created by that invocation.

The LP controls continuous areas. Whole-shape selection can leave windows below
the requested minimum; reported post-fill density and violation counts reflect
actual placement against the original requested bounds. Areas are accumulated
from non-overlapping tile-contained fill shapes. The log reports preparation,
solver, and placement times separately, excluding design loading.

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
        "space_to_non_fill": "<real: spacing to non-fill shapes in microns>",
        "stagger": "ON or OFF (default OFF)",
        "stagger_offset": ["<X offset in microns>", "<Y offset in microns>"]
      }
    }, ...
  }
}
```

**Staggered non-OPC fill**

The optional `stagger` setting is the string `"ON"` or `"OFF"`, with `"OFF"`
as the default. `"ON"` requires `stagger_offset` to be an array of two finite
signed distances **in microns**, not fractions of the fill pitch. At most one
component may be nonzero: use `[x, 0]` for X stagger or `[0, y]` for Y stagger.
Specifying nonzero offsets on both axes is an error, including when stagger is
OFF. For example:

```json
"non-opc": {
  "datatype": 0,
  "width": [2.00, 1.00, 0.58, 0.3],
  "height": [2.00, 1.00, 0.58, 0.3],
  "space_to_fill": 0.3,
  "space_to_non_fill": 3,
  "stagger": "ON",
  "stagger_offset": [0.5, 0.0]
}
```

X displaces odd rows horizontally; Y displaces odd columns vertically. For
logical row `r` and column `c`, shape width `w`, height `h`, and fill spacings
`sx` and `sy`, the lower-left coordinate is:

- `x = origin_x + c * (w + sx) + (r mod 2) * X_OFFSET`
- `y = origin_y + r * (h + sy) + (c mod 2) * Y_OFFSET`

The origin is the fill area's lower-left corner for `density_fill`, and the
core lower-left corner for `lp_fill`. It is shared across polygons and LP tiles.
The same physical offset applies to every configured shape size. All shapes
remain inside their fillable regions. Selection follows candidate generation
order for both staggered and non-staggered patterns.

An omitted `stagger` setting or explicit `"OFF"` preserves the existing
polygon-local grid. Offsets provided with `"OFF"` are validated but do not move
shapes. `"ON"` with `[0, 0]` produces a globally aligned grid.

The `opc` section is optional depending on your process.

The width/height lists are effectively parallel arrays of shapes to try
in left to right order (generally larger to smaller).

The layer grouping is for convenience. For example in some technologies many
layers have similar rules so it is convenient to have a `Mx`, `Cx` group.

This all started out in `klayout` so there are some obsolete fields that the
parser accepts but ignores (e.g., `space_to_outline`).

## Regression tests

There are a set of regression tests in `./test`. For more information, refer to this [section](../../README.md#regression-tests). 

Simply run the following command:

```shell
bazelisk test //openroad/fin/...
```

## Limitations

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

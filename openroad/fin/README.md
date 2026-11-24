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
    -rules rules_file
    [-area {lx ly ux uy}]
```

#### Options

| Switch Name | Description | 
| ----- | ----- |
| `-rules` | Specify `json` rule file. |
| `-area` | Optional. If not specified, the core area will be used. |

### LP Fill

Optimize metal fill using a linear program (LP) and place fill rectangles to achieve the window density targets determined by the LP, using the same JSON rule format as `density_fill`.

The LP Fill implementation was developed with reference to [1] and [2],
particularly their LP formulations and layout density control objectives.

```tcl
lp_fill
    -rules rules_file
    -objective min_var|min_fill
    -window_size {width height}
    -window_step {x_ratio y_ratio}
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
| `-rules` | Required. Specify JSON fill rule file. LP fill currently supports non-OPC rules only. |
| `-objective` | Required. `min_var` maximizes the minimum window density [1], rather than minimizing statistical variance; `min_fill` minimizes total fill area [2].|
| `-window_size` | Required. window width and height for density check, in microns.|
| `-window_step` | Required. Horizontal and vertical step ratios relative to window width and height, respectively. Each ratio is in `(0, 1]`; `0.25` means 25% of the corresponding window dimension. |
| `-solver` | Optional. `glop` (default) or `pdlp`, both open-source OR-Tools LP solvers. |
| `-solver_threads` | Optional. Number of CPU threads used by the LP solver (Only for PDLP). |
| `-num_threads` | Optional. Number of CPU threads used for fill processing outside the LP solver.|
| `-min_density` | Optional. Minimum post-fill window density, from `0` to `1`; default `0.20`. |
| `-max_density` | Optional. Maximum post-fill window density, from `0` to `1` and at least the minimum; default `0.70`. |
| `-min_tile_density` | Optional. Minimum post-fill tile density, from `0` to `1`; default `0`.|
| `-max_tile_density` | Optional. Maximum post-fill tile density, from `0` to `1` and at least the tile minimum; default `1`.|


For example, `-window_size {50 30} -window_step {0.25 0.5}` uses actual steps
of `12.5` microns horizontally and `15` microns vertically. Step ratios are
converted to distances and rounded to database units before constructing the
tile/window grid.

The window dimensions and window step ratios determine the tile partition used in the LP formulation. 
For example, a 50 × 50 µm window with step ratios of 0.25 × 0.25 produces 12.5 × 12.5 µm tiles.
Each LP variable represents the fill area to be added to one tile.
Without tile density bounds, fill may concentrate in certain tiles while still satisfying the window density constraints.
Specifying minimum and maximum tile densities helps control this local concentration.

## Example scripts

The rules `json` file controls fill and you can see an example
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
        "height":   "<list of heights in microns>",
        "space_to_fill": "<real: spacing between fills in microns>",
        "space_to_non_fill": "<real: spacing to non-fill shapes in microns>",
        "stagger": "ON or OFF (default OFF)",
        "stagger_offset": ["<X offset in microns>", "<Y offset in microns>"]
      }
    }, ...
  }
}
```

The opc section is optional depending on your process.

The width/height lists are effectively parallel arrays of shapes to try in left to right order (generally larger to smaller).

The layer grouping is for convenience. For example in some technologies many layers have similar rules so it is convenient to have a Mx, Cx group.

This all started out in klayout so there are some obsolete fields that the parser accepts but ignores (e.g., space_to_outline).

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

The staggered option for `opc` section is not currently supported.

This configuration format originated in KLayout (except for `stagger` and
`stagger_offset`, which were newly added). Some legacy fields are accepted by
the parser but ignored (e.g., `space_to_outline`).

## Regression tests

There are a set of regression tests in `./test`. For more information, refer to this [section](../../README.md#regression-tests). 

Simply run the following command:

```shell
bazelisk test //openroad/fin/...
```

## Authors

The LP-based fill insertion implementation and stagger option were developed
by [Jinoh Cho](https://jinoh-cho.github.io/)
([jinoma@postech.ac.kr](mailto:jinoma@postech.ac.kr)), a Ph.D. Candidate in [CSDL Lab](https://csdl.postech.ac.kr/) (under [Prof. Seokhyeong Kang](https://csdl.postech.ac.kr/bbs/board.php?bo_table=sub3_1)) at
POSTECH (Pohang University of Science and Technology), during his visiting graduate scholar appointment with [UCSD ABKGroup (UCSD VLSI CAD Lab.)](https://vlsicad.ucsd.edu/) at UC San Diego, under the guidance of
[Prof. Andrew B. Kahng](https://vlsicad.ucsd.edu/~abk/).

## References

[1] A. B. Kahng, G. Robins, A. Singh, and A. Zelikovsky,
"[Filling Algorithms and Analyses for Layout Density Control](https://vlsicad.ucsd.edu/Publications/Journals/j40.pdf),"
*IEEE Transactions on Computer-Aided Design of Integrated Circuits and Systems
(TCAD)*, vol. 18, no. 4, pp. 445–462, 1999.

[2] Y. Chen, A. B. Kahng, G. Robins, and A. Zelikovsky,
"[Practical Iterated Fill Synthesis for CMP Uniformity](https://www.cs.virginia.edu/~robins/papers/dac2000_camera.pdf),"
in *Proceedings of the ACM/IEEE Design Automation Conference (DAC)*,
pp. 671–674, 2000.

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

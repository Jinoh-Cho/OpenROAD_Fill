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

### Minimum-Variation Fill

`min_var_fill` currently uses the same density-fill baseline as a starting
point for the minimum-variation algorithm. Its implementation is independent
from `DensityFill`, so candidate selection and tile-density optimization can
be changed without modifying the established density-fill path.

```tcl
min_var_fill
    [-rules rules_file]
    [-area {lx ly ux uy}]
```

The options have the same meaning as `density_fill`.

### Debugging

Enable interactive GUI rendering before invoking the corresponding command.
The GUI must be enabled for the debug views to be displayed.

<!-- checker: skip -->
```tcl
density_fill_debug
density_fill -rules fill.json

min_var_fill_debug
min_var_fill -rules fill.json
```

### Rectangle extraction benchmark

This experimental command measures fill-area rectangle decomposition after
replicating the input area. It does not insert fill shapes.

```tcl
density_fill_rectangle_extraction_benchmark
    [-rules rules_file]
    [-area {lx ly ux uy}]
    [-left copies]
    [-right copies]
    [-bottom copies]
    [-top copies]
    [-runs runs]
```

### Tile-grid metal area

This experimental command partitions the selected area into tiles and reports
the metal area. It can also write one SVG per configured layer.

```tcl
tile_grid_metal_area
    [-rules rules_file]
    [-area {lx ly ux uy}]
    -window window_size
    [-origin {x y}]
    [-resolution resolution]
    [-svg file]
```

## Source architecture

The FIN module has two fill implementations that share rule parsing and
geometry collection. The normal OpenROAD density-fill implementation remains
in `DensityFill`; experimental minimum-variation work belongs in
`MinVarFill`.

```text
Tcl commands (finale.tcl)
        |
        v
SWIG wrappers (finale.i) --> Finale (Finale.h / Finale.cpp)
        |                         |
        |                         +--> DensityFill: established fill algorithm
        |                         |
        |                         +--> MinVarFill: minimum-variation algorithm
        v
FillConfig: JSON rules          FillGeometry: OpenDB shapes -> Polygon90Set
        |                         |
        +-------------+-----------+
                      v
              OpenDB / Boost Polygon
```

### Source files and responsibilities

#### Original OpenROAD FIN layout

Before the MinVar work, FIN was centered on one implementation,
`DensityFill`. The rule parser and existing-metal geometry collector were
private helpers in `DensityFill.cpp`.

```text
finale.tcl --> finale.i --> Finale --> DensityFill
                                      |
                                      +--> JSON rule parsing (private)
                                      +--> non-fill geometry collection (private)
                                      +--> Boost Polygon fill generation
                                      +--> OpenDB dbFill insertion
                                      +--> Graphics (GUI debug only)
```

| Original file | Original role | Original dependencies |
| --- | --- | --- |
| `include/fin/Finale.h`, `src/Finale.cpp` | Public FIN facade owned by `ord::OpenRoad`; creates `DensityFill` and forwards `density_fill`. | OpenDB, logger, `DensityFill` |
| `include/fin/MakeFinale.h`, `src/MakeFinale.cpp` | Initializes the FIN Tcl package. | Tcl, encoded Tcl scripts, SWIG module |
| `src/finale.i` | SWIG wrappers for C++ FIN operations. | `Finale`, `ord::OpenRoad` |
| `src/finale.tcl` | Defines `density_fill`; validates options and converts microns to DBU. | Tcl STA utilities, OpenDB Tcl API, SWIG wrappers |
| `src/DensityFill.h`, `src/DensityFill.cpp` | The original density-fill algorithm and its private JSON parsing, shape collection, spacing/pruning, and `dbFill` insertion helpers. | Boost Property Tree, Boost Polygon, OpenDB, logger, GUI |
| `src/graphics.h`, `src/graphics.cpp` | Draws intermediate polygon areas when GUI debugging is enabled. | OpenROAD GUI, Boost Polygon |
| `src/polygon.h` | Local aliases for Boost Polygon geometry types and operators. | Boost Polygon |
| `src/fin/CMakeLists.txt`, `src/fin/BUILD` | Registers the FIN library, Tcl/SWIG bindings, and dependencies in CMake and Bazel. | CMake/Bazel, OpenDB, GUI, Boost, Tcl |

#### Current layout: shared infrastructure and MinVar

The original private helpers were extracted without changing the established
`DensityFill` algorithm. Both implementations now use the same rule and
geometry inputs, but only `MinVarFill` is intended to diverge in its future
candidate-selection step.

| Current file | Relationship to original layout | Role | Main dependencies |
| --- | --- | --- | --- |
| `include/fin/Finale.h`, `src/Finale.cpp` | Extended original facade | Dispatches both `DensityFill` and `MinVarFill`; stores separate debug flags. | `DensityFill`, `MinVarFill`, OpenDB, logger |
| `src/DensityFill.h`, `src/DensityFill.cpp` | Original implementation retained | Established OpenROAD density-fill algorithm: legal non-OPC/OPC regions, pruning, and `dbFill` insertion. | `FillConfig`, `FillGeometry`, Boost Polygon, OpenDB, GUI |
| `src/FillConfig.h`, `src/FillConfig.cpp` | Extracted from original `DensityFill.cpp` | Shared JSON parser. Expands grouped rules into `FillLayerConfigs`, keyed by `odb::dbTechLayer*`. | Boost Property Tree, OpenDB, logger |
| `src/FillGeometry.h`, `src/FillGeometry.cpp` | Extracted from original `DensityFill.cpp` | Shared `makeRect`, `insertShape`, and `orNonFills` helpers. | OpenDB, Boost Polygon |
| `src/MinVarFill.h`, `src/MinVarFill.cpp` | New implementation | Independent density-fill baseline for the minimum-variation algorithm. Future variation-minimizing candidate selection belongs here. | `FillConfig`, `FillGeometry`, `TileGrid`, Boost Polygon, OpenDB, GUI |
| `include/fin/TileGrid.h`, `src/TileGrid.cpp` | New analysis utility | Divides an area into windows and tiles; calculates per-tile metal density and writes optional SVG visualizations. | Boost Polygon, OpenDB |
| `src/graphics.h`, `src/graphics.cpp` | Unchanged original shared utility | GUI renderer used by both fill implementations in debug mode. | OpenROAD GUI, Boost Polygon |
| `src/finale.i`, `src/finale.tcl` | Extended original command layer | Adds `min_var_fill` and `min_var_fill_debug` alongside the original density-fill commands. | `Finale`, SWIG, Tcl |

#### Complete FIN file inventory

The tables above describe the main algorithm paths. This inventory covers all
remaining source, binding, build, and test files in `src/fin` as well, so it
can be used to compare the original FIN structure with the current layout.

| File | Role |
| --- | --- |
| `README.md` | User documentation, command synopsis, architecture, and JSON rule format. |
| `CMakeLists.txt` | CMake registration of the FIN library, Tcl SWIG library, Python binding, and tests. |
| `BUILD` | Bazel registration of the FIN library, Tcl/Python SWIG wrappers, generated message metadata, and test-visible documentation. |
| `include/fin/Finale.h` | Public `Finale` facade declaration. |
| `include/fin/MakeFinale.h` | Public declaration of `initFinale()`, called while initializing OpenROAD. |
| `include/fin/TileGrid.h` | Public declaration of tile/window geometry and metal-density analysis APIs. |
| `src/Finale.cpp` | Implements the `Finale` facade and selects density-fill or MinVarFill. |
| `src/MakeFinale.cpp` | Registers the compiled Tcl/SWIG FIN package during startup. |
| `src/finale.i` | Tcl SWIG wrappers for all FIN Tcl-to-C++ calls. |
| `src/finale-py.i` | Python SWIG interface; exposes the public `Finale` API to Python. |
| `src/finale.tcl` | Tcl command definitions, option validation, and unit conversion. |
| `src/DensityFill.h`, `src/DensityFill.cpp` | Original OpenROAD density-fill implementation. |
| `src/MinVarFill.h`, `src/MinVarFill.cpp` | Independent baseline and future implementation location for minimum-variation fill. |
| `src/FillConfig.h`, `src/FillConfig.cpp` | Shared fill-rule configuration types and JSON parser. |
| `src/FillGeometry.h`, `src/FillGeometry.cpp` | Shared OpenDB-to-Boost-Polygon geometry conversion helpers. |
| `src/TileGrid.cpp` | Implements the public `TileGrid` density and SVG analysis API. |
| `src/graphics.h`, `src/graphics.cpp` | FIN GUI debug renderer. |
| `src/polygon.h` | Local Boost Polygon aliases and operators. |

| Test or support file | Role |
| --- | --- |
| `test/CMakeLists.txt`, `test/BUILD` | Register FIN regression tests in CMake and Bazel. |
| `test/fill.json` | Fill rule input shared by FIN test scripts. |
| `test/gcd_prefill.def` | Small placed/routed design used as the common test input. |
| `test/gcd_fill.tcl`, `test/gcd_fill.py` | Tcl and Python smoke tests for the original density-fill command. |
| `test/gcd_fill.ok`, `test/gcd_fill.defok` | Golden log and DEF output for `gcd_fill`. |
| `test/min_var_fill.tcl` | Pass/fail smoke test and directly runnable Tcl script for `min_var_fill`. |
| `test/gcd_fill_rectangle_benchmark.tcl` | Regression/pass-fail driver for rectangle-extraction benchmarking. |
| `test/gcd_fill_svg.tcl` | Batch-mode SVG export driver for fill-area visualization. |
| `test/gcd_tile_grid_density.tcl` | Verifies total metal area remains invariant across tile/grid dissections. |
| `test/run_fin_rectangle_scaling.sh` | Runs rectangle-extraction scaling experiments over copy counts. |
| `test/run_gcd_fill_svg_sweep.sh` | Generates SVG results for a sweep of non-OPC spacing values. |
| `test/fin_readme_msgs_check.py`, `test/fin_readme_msgs_check.ok` | Validates FIN README/message metadata generated by the project checks. |

#### Dependency comparison

```text
Original OpenROAD path
  density_fill -> Finale -> DensityFill
                            |- private rule parser
                            |- private orNonFills collector
                            `- fill placement

Current shared path
  density_fill -> Finale -> DensityFill -+
                                        |- FillConfig
  min_var_fill -> Finale -> MinVarFill -+-- FillGeometry -> OpenDB / Boost Polygon
                                        `- TileGrid (MinVar analysis)
```

### Data flow

1. `density_fill` or `min_var_fill` parses Tcl options in `finale.tcl`.
2. `finale.i` calls the appropriate `Finale` method through SWIG.
3. `Finale` creates `DensityFill` or `MinVarFill`.
4. The implementation loads per-layer rules through `FillConfig` and obtains
   existing metal geometry with `orNonFills` from `FillGeometry`.
5. Boost Polygon computes legal fill regions; the implementation inserts
   selected rectangles into OpenDB as `dbFill` objects.
6. In GUI debug mode, `Graphics` renders the intermediate polygon regions.

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

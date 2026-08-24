# Export the non-OPC fill area as SVG without requiring the OpenROAD GUI.
# Run from the repository root with:
#   bazel-bin/openroad src/fin/test/gcd_fill_svg.tcl

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ../../..]]

read_lef "$repo_root/test/sky130hd/sky130hd.tlef"
read_lef "$repo_root/test/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

# This also enables SVG export in DensityFill, even in batch mode.
density_fill_debug
density_fill -rules "$script_dir/fill.json" -area {0 0 100 100}

exit 0

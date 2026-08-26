# Export the non-OPC fill area as SVG without requiring the OpenROAD GUI.
# Run from the repository root with:
#   bazel-bin/openroad src/fin/test/gcd_fill_svg.tcl

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ../../..]]
set rules_file "$script_dir/fill.json"
if { [info exists ::env(FIN_FILL_RULES)] } {
  set rules_file $::env(FIN_FILL_RULES)
}
if { [info exists ::env(FIN_FILL_SVG_DIR)] } {
  file mkdir $::env(FIN_FILL_SVG_DIR)
  cd $::env(FIN_FILL_SVG_DIR)
}

read_lef "$repo_root/test/sky130hd/sky130hd.tlef"
read_lef "$repo_root/test/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

# This also enables SVG export in DensityFill, even in batch mode.
density_fill_debug
density_fill -rules $rules_file

exit 0

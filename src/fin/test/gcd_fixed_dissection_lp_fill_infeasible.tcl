# Keep SVG output and skip fill when a tile-density minimum is infeasible.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

set svg_file [make_result_file fixed_dissection_lp_fill_infeasible.svg]
set placed_fill [fixed_dissection_lp_fill \
  -rules "$script_dir/fill.json" \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -min_tile_density 1.0 \
  -max_density 0.75 \
  -svg $svg_file]

if {$placed_fill != 0.0} {
  error "Infeasible LP fill unexpectedly placed fill area."
}
if { ![file exists "${svg_file}_met1.svg"] } {
  error "Infeasible LP fill did not write the fill-excluded SVG."
}
puts "pass"

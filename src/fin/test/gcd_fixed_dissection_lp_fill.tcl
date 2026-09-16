# Place fill rectangles from the fixed-dissection LP tile targets.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

if {[info exists ::env(RESULTS_DIR)]} {
  set svg_file "$::env(RESULTS_DIR)/fixed_dissection_lp_fill.svg"
} else {
  set svg_file "$script_dir/fixed_dissection_lp_fill.svg"
}
set placed_fill [fixed_dissection_lp_fill \
  -rules "$script_dir/fill.json" \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -min_tile_density 0.05 \
  -max_density 0.45 \
  -svg $svg_file]

if {$placed_fill <= 0.0} {
  error "Fixed-dissection LP fill did not place any fill area."
}
if { ![file exists "${svg_file}_met1.svg"] } {
  error "Fixed-dissection LP fill did not write the placement SVG."
}
if { ![file exists "${svg_file}_met1_fillable.svg"] } {
  error "Fixed-dissection LP fill did not write the fillable-region SVG."
}
puts "placed_fill_area=$placed_fill DBU^2"
puts "pass"

# Run FIN's original density-fill algorithm, then export the placed dbFill
# rectangles as SVG.  In the SVG, existing wire is gray and actual fill is blue.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/prefill_bench/gcd_prefill.def"

if {[info exists ::env(RESULTS_DIR)]} {
  set svg_file "$::env(RESULTS_DIR)/gcd_density_fill.svg"
  set density_report "$::env(RESULTS_DIR)/gcd_density_fill_density.json"
} else {
  set svg_file "$script_dir/gcd_density_fill.svg"
  set density_report "$script_dir/gcd_density_fill_density.json"
}

# This is the original OpenROAD FIN fill algorithm, not an LP-based fill.
set density_fill_start [clock milliseconds]
density_fill -rules "$script_dir/fill.json"
puts [format "FIN-DENSITY-FILL-RUNTIME: density_fill=%.3fs (SVG excluded)." \
  [expr {([clock milliseconds] - $density_fill_start) / 1000.0}]]

set metal_area [tile_grid_metal_area \
  -rules "$script_dir/fill.json" \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -max_density 0.45 \
  -svg $svg_file \
  -density_report $density_report]

if {$metal_area <= 0.0} {
  error "Original density_fill produced no metal area."
}
if { ![file exists "${svg_file}_met1.svg"] } {
  error "Original density_fill did not write the placement SVG."
}
puts "post_fill_metal_area=$metal_area DBU^2"
puts "pass"
exit

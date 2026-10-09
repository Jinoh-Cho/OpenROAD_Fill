# Place fill rectangles from the fixed-dissection LP tile targets.
set script_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $script_dir]
source "$test_dir/helpers.tcl"

read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$test_dir/prefill_bench/gcd_prefill.def"

set run_date [string trim [exec date +%Y%m%d]]
set results_dir [file normalize "$test_dir/results/$run_date/gcd"]
file mkdir $results_dir
set svg_file "$results_dir/gcd_fixed_dissection_lp_fill.svg"
set density_report "$results_dir/gcd_fixed_dissection_lp_fill_density.json"
set placed_fill [fixed_dissection_lp_min_var_fill \
  -rules "$test_dir/fill.json" \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -min_tile_density 0.20 \
  -min_window_density 0.30 \
  -max_window_density 0.60 \
  -svg $svg_file \
  -density_report $density_report]

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
exit

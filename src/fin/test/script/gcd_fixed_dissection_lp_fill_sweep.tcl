# Run one GCD fixed-dissection LP window-density sweep point.
foreach variable {FIN_MIN_WINDOW_DENSITY FIN_MAX_WINDOW_DENSITY} {
  if {![info exists ::env($variable)]} {
    error "$variable must be set."
  }
}
set min_window_density $::env(FIN_MIN_WINDOW_DENSITY)
set max_window_density $::env(FIN_MAX_WINDOW_DENSITY)
if {![string is double -strict $min_window_density]
    || ![string is double -strict $max_window_density]
    || $min_window_density < 0.0 || $max_window_density > 1.0
    || $min_window_density > $max_window_density} {
  error "Window-density limits must satisfy 0.0 <= min <= max <= 1.0."
}

set script_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $script_dir]
source "$test_dir/helpers.tcl"
set rules "$test_dir/fill.json"
if {[info exists ::env(FIN_FILL_RULES)]} {
  set rules [file normalize $::env(FIN_FILL_RULES)]
}
if {![file exists $rules]} {
  error "Fill rules file does not exist: $rules"
}
set rules_tag [file rootname [file tail $rules]]

read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$test_dir/prefill_bench/gcd_prefill.def"

set run_date [string trim [exec date +%Y%m%d]]
set min_tag [string map {. _} [format %.2f $min_window_density]]
set max_tag [string map {. _} [format %.2f $max_window_density]]
set results_dir [file normalize "$test_dir/results/$run_date/gcd/window_density_sweep/$rules_tag/min_$min_tag/max_$max_tag"]
file mkdir $results_dir
set result_name "gcd_fixed_dissection_lp_fill_min_${min_tag}_max_${max_tag}"
set svg_file "$results_dir/$result_name.svg"
set density_report "$results_dir/${result_name}_density.json"

set placed_fill [fixed_dissection_lp_fill \
  -rules $rules \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -min_tile_density 0.20 \
  -min_window_density $min_window_density \
  -max_window_density $max_window_density \
  -svg $svg_file \
  -density_report $density_report]

if {$placed_fill <= 0.0} {
  error "Fixed-dissection LP fill did not place any fill area."
}
puts "min_window_density=$min_window_density"
puts "max_window_density=$max_window_density"
puts "placed_fill_area=$placed_fill DBU^2"
puts "pass"
exit

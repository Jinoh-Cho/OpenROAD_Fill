# Shared worker: run one method on a fresh GCD or IBEX layout.
foreach required {FIN_FILL_METHOD FIN_OUTPUT_DIR FIN_RULES} {
  if {![info exists ::env($required)]} {error "$required must be specified."}
}
set method $::env(FIN_FILL_METHOD)
if {$method ni {density lp_minvar min_amount lip1 lip2 lip3}} {
  error "Unknown FIN_FILL_METHOD: $method"
}
proc comparison_option {name default} {
  if {[info exists ::env($name)]} {return $::env($name)}
  return $default
}
set benchmark [comparison_option FIN_BENCHMARK gcd]
if {$benchmark ni {gcd ibex}} {error "Unsupported benchmark: $benchmark"}
set resolution [comparison_option FIN_RESOLUTION 2]
set window [comparison_option FIN_WINDOW 50]
set min_tile [comparison_option FIN_MIN_TILE_DENSITY 0.20]
set max_tile [comparison_option FIN_MAX_TILE_DENSITY 1.0]
set min_window [comparison_option FIN_MIN_WINDOW_DENSITY 0.30]
set max_window [comparison_option FIN_MAX_WINDOW_DENSITY 0.60]
set algorithms [comparison_option FIN_FLOATING_DENSITY_ALGORITHMS {alg2 alg3}]
if {![llength $algorithms]} {error "At least one density algorithm is required."}
foreach algorithm $algorithms {
  if {$algorithm ni {alg2 alg3}} {error "Unknown algorithm: $algorithm"}
}
set output [file normalize $::env(FIN_OUTPUT_DIR)]
file mkdir $output
if {[file exists "$output/run_summary.json"]} {
  error "Results already exist in $output; use a new output directory."
}
set rules [file normalize $::env(FIN_RULES)]
set commands [dict create density density_fill \
  lp_minvar fixed_dissection_lp_min_var_fill \
  min_amount fixed_dissection_lp_min_amount_fill \
  lip1 fixed_dissection_lp_lip_fill lip2 fixed_dissection_lp_lip_fill \
  lip3 fixed_dissection_lp_lip_fill]
set command [dict get $commands $method]
foreach required [list $command tile_grid_metal_area] {
  if {![llength [info commands $required]]} {
    error "Missing command $required. Rebuild OpenROAD with the current FIN sources."
  }
}

set test_dir [file dirname [file dirname [file normalize [info script]]]]
read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$test_dir/prefill_bench/${benchmark}_prefill.def"
set analysis_options [list -rules $rules -window $window -origin {0 0} \
  -resolution $resolution -min_window_density $min_window -max_density $max_window]
set before_area [tile_grid_metal_area {*}$analysis_options \
  -density_report "$output/before_density.json"]
set placed_area null
set fill_start [clock milliseconds]
if {$method eq "density"} {
  density_fill -rules $rules
} else {
  set fill_options [list -rules $rules -window $window -origin {0 0} \
    -resolution $resolution -min_tile_density $min_tile -max_tile_density $max_tile \
    -min_window_density $min_window -max_window_density $max_window \
    -density_report "$output/lp_fill_report.json"]
  if {[string match lip* $method]} {lappend fill_options -lip_type [string index $method end]}
  if {[comparison_option FIN_WRITE_SVG 0]} {lappend fill_options -svg "$output/placed_fill"}
  set placed_area [$command {*}$fill_options]
}
set fill_seconds [expr {([clock milliseconds] - $fill_start) / 1000.0}]
set analysis_start [clock milliseconds]
foreach algorithm $algorithms {
  set options $analysis_options
  lappend options -density_report "$output/density_${algorithm}.json" \
    -floating_density_profile "$output/floating_${algorithm}" \
    -floating_density_algorithm $algorithm
  if {[comparison_option FIN_WRITE_SVG 0] && $algorithm eq [lindex $algorithms end]} {
    lappend options -svg "$output/post_fill"
  }
  set after_area [tile_grid_metal_area {*}$options]
}
set analysis_seconds [expr {([clock milliseconds] - $analysis_start) / 1000.0}]
set added_area [expr {$after_area - $before_area}]
set report [open "$output/run_summary.json" w]
puts $report "{\"method\": \"$method\", \"fill_seconds\": $fill_seconds, \"analysis_seconds\": $analysis_seconds, \"placed_fill_area_dbu2\": $placed_area, \"added_metal_area_dbu2\": $added_area}"
close $report
puts "COMPARISON_RESULT: method=$method fill_seconds=$fill_seconds added_metal_area=$added_area DBU^2"
puts "pass"
exit

# Shared worker: run one method on a fresh GCD or IBEX layout.
# FIN_INPUT_DEF overrides the benchmark DEF. Scalability runs may disable
# density analysis and report I/O independently; existing defaults are retained.
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
set lp_solver [comparison_option FIN_LP_SOLVER glop]
set lp_threads [comparison_option FIN_LP_THREADS 1]
set candidate_threads [comparison_option FIN_CANDIDATE_THREADS 1]
set placement_threads [comparison_option FIN_PLACEMENT_THREADS 1]
set resolution [comparison_option FIN_RESOLUTION 2]
set window [comparison_option FIN_WINDOW 50]
set min_tile [comparison_option FIN_MIN_TILE_DENSITY 0.20]
set max_tile [comparison_option FIN_MAX_TILE_DENSITY 1.0]
set min_window [comparison_option FIN_MIN_WINDOW_DENSITY 0.30]
set max_window [comparison_option FIN_MAX_WINDOW_DENSITY 0.60]
set algorithms [comparison_option FIN_FLOATING_DENSITY_ALGORITHMS {alg2 alg3}]
set run_analysis [comparison_option FIN_RUN_ANALYSIS 1]
set write_reports [comparison_option FIN_WRITE_REPORTS 1]
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
foreach required [list $command tile_grid_metal_area fin::layout_feature_counts_cmd] {
  if {![llength [info commands $required]]} {
    error "Missing command $required. Rebuild OpenROAD with the current FIN sources."
  }
}

set test_dir [file dirname [file dirname [file normalize [info script]]]]
set input_def [comparison_option FIN_INPUT_DEF "$test_dir/prefill_bench/${benchmark}_prefill.def"]
set load_start [clock milliseconds]
read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def $input_def
set load_seconds [expr {([clock milliseconds] - $load_start) / 1000.0}]
set analysis_options [list -rules $rules -window $window -origin {0 0} \
  -resolution $resolution -min_window_density $min_window -max_density $max_window]
set before_seconds 0.0
if {$run_analysis} {
  set before_start [clock milliseconds]
  set before_options $analysis_options
  if {$write_reports} {lappend before_options -density_report "$output/before_density.json"}
  set before_area [tile_grid_metal_area {*}$before_options]
  set before_seconds [expr {([clock milliseconds] - $before_start) / 1000.0}]
}
set placed_area null
set fill_start [clock milliseconds]
if {$method eq "density"} {
  density_fill -rules $rules
} else {
  set fill_options [list -lp_diagnostics "$output/lp_failure" -lp_solver $lp_solver -lp_threads $lp_threads -candidate_threads $candidate_threads -placement_threads $placement_threads -rules $rules -window $window -origin {0 0} \
    -resolution $resolution -min_tile_density $min_tile -max_tile_density $max_tile \
    -min_window_density $min_window -max_window_density $max_window]
  if {$write_reports} {lappend fill_options -density_report "$output/lp_fill_report.json"}
  if {[string match lip* $method]} {lappend fill_options -lip_type [string index $method end]}
  if {[comparison_option FIN_WRITE_SVG 0]} {lappend fill_options -svg "$output/placed_fill"}
  set placed_area [$command {*}$fill_options]
}
set fill_seconds [expr {([clock milliseconds] - $fill_start) / 1000.0}]
set feature_count_start [clock milliseconds]
set feature_counts [fin::layout_feature_counts_cmd]
set feature_count_seconds [expr {([clock milliseconds] - $feature_count_start) / 1000.0}]
puts "LAYOUT_FEATURE_COUNTS: $feature_counts"
set feature_count_summary ""
foreach name {fill_count wire_count regular_wire_count special_wire_count} {
  regexp [format {"%s": ([0-9]+)} $name] $feature_counts -> value
  append feature_count_summary " $name=$value"
}
set analysis_seconds 0.0
set added_area null
if {$run_analysis} {
  set analysis_start [clock milliseconds]
  foreach algorithm $algorithms {
    set options $analysis_options
    lappend options -floating_density_algorithm $algorithm
    if {$write_reports} {
      lappend options -density_report "$output/density_${algorithm}.json" \
        -floating_density_profile "$output/floating_${algorithm}"
    }
    if {[comparison_option FIN_WRITE_SVG 0] && $algorithm eq [lindex $algorithms end]} {
      lappend options -svg "$output/post_fill"
    }
    set after_area [tile_grid_metal_area {*}$options]
  }
  set analysis_seconds [expr {([clock milliseconds] - $analysis_start) / 1000.0}]
  set added_area [expr {$after_area - $before_area}]
}
set report [open "$output/run_summary.json" w]
puts $report "{\"method\": \"$method\", \"load_seconds\": $load_seconds, \"before_analysis_seconds\": $before_seconds, \"fill_seconds\": $fill_seconds, \"analysis_seconds\": $analysis_seconds, \"placed_fill_area_dbu2\": $placed_area, \"added_metal_area_dbu2\": $added_area, \"feature_count_seconds\": $feature_count_seconds, [string range $feature_counts 1 end-1]}"
close $report
puts "COMPARISON_RESULT: method=$method fill_seconds=$fill_seconds added_metal_area=$added_area DBU^2$feature_count_summary"
puts "pass"
exit

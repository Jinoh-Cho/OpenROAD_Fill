# Run one fill method on a fresh GCD prefill layout, then compare ALG2 and ALG3.
if {![info exists ::env(FIN_FILL_METHOD)]} {
  error "FIN_FILL_METHOD must be density, lp_minvar, or min_amount."
}
set method $::env(FIN_FILL_METHOD)
if {$method ni {density lp_minvar min_amount}} {
  error "Unknown FIN_FILL_METHOD: $method"
}
set resolution 2
if {[info exists ::env(FIN_RESOLUTION)]} {
  set resolution $::env(FIN_RESOLUTION)
}
set min_tile_density 0.20
if {[info exists ::env(FIN_MIN_TILE_DENSITY)]} {
  set min_tile_density $::env(FIN_MIN_TILE_DENSITY)
}
set results_group fill_method_comparison
if {[info exists ::env(FIN_RESULTS_GROUP)]} {
  set results_group $::env(FIN_RESULTS_GROUP)
}
set results_tag $method
if {[info exists ::env(FIN_RESULTS_TAG)]} {
  set results_tag $::env(FIN_RESULTS_TAG)
}

set script_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $script_dir]
source "$test_dir/helpers.tcl"

read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$test_dir/prefill_bench/gcd_prefill.def"

set run_date [string trim [exec date +%Y%m%d]]
set results_dir [file normalize "$test_dir/results/$run_date/gcd/$results_group/$results_tag"]
file mkdir $results_dir
set rules "$test_dir/fill_tiny_pattern.json"
if {[info exists ::env(FIN_RULES)]} {
  set rules $::env(FIN_RULES)
}

set fill_start [clock milliseconds]
switch -- $method {
  density {
    density_fill -rules $rules
  }
  lp_minvar {
    # This is the actual LP objective: maximize the minimum window fill area.
    # Do not use min_var_fill here; it currently only generates candidates.
    set placed_fill [fixed_dissection_lp_fill \
      -rules $rules \
      -window 50 \
      -origin {0 0} \
      -resolution $resolution \
      -min_tile_density $min_tile_density \
      -min_window_density 0.30 \
      -max_window_density 0.60]
    puts "placed_fill_area=$placed_fill DBU^2"
  }
  min_amount {
    set placed_fill [fixed_dissection_lp_min_amount_fill \
      -rules $rules \
      -window 50 \
      -origin {0 0} \
      -resolution $resolution \
      -min_tile_density $min_tile_density \
      -min_window_density 0.30 \
      -max_window_density 0.60]
    puts "placed_fill_area=$placed_fill DBU^2"
  }
}
puts [format "FILL_RUNTIME: method=%s resolution=%s seconds=%.3f" $method $resolution \
  [expr {([clock milliseconds] - $fill_start) / 1000.0}]]

if {[info exists ::env(FIN_SKIP_PROFILE)] && $::env(FIN_SKIP_PROFILE)} {
  puts "pass (profile extraction skipped)"
  exit
}

set algorithms {alg2 alg3}
if {[info exists ::env(FIN_FLOATING_DENSITY_ALGORITHMS)]} {
  set algorithms $::env(FIN_FLOATING_DENSITY_ALGORITHMS)
}
foreach algorithm $algorithms {
  if {$algorithm ni {alg2 alg3}} {
    error "Unknown floating-density algorithm '$algorithm'; expected alg2 or alg3."
  }
  set profile_prefix "$results_dir/gcd_${method}_${algorithm}_floating_density"
  set analysis_start [clock milliseconds]
  tile_grid_metal_area \
    -rules $rules \
    -window 50 \
    -origin {0 0} \
    -resolution $resolution \
    -min_window_density 0.30 \
    -max_density 0.60 \
    -floating_density_profile $profile_prefix \
    -floating_density_algorithm $algorithm
  puts [format "FLOATING_DENSITY_RUNTIME: algorithm=%s seconds=%.3f" $algorithm \
    [expr {([clock milliseconds] - $analysis_start) / 1000.0}]]
  puts "FLOATING_DENSITY_PROFILE_PREFIX=$profile_prefix"
}

puts "pass"
exit

# Run one Lipschitz LP fill on a fresh GCD prefill, then extract exact
# floating-window density profiles with both J40 analysis algorithms.
if {![info exists ::env(FIN_LIP_TYPE)] || $::env(FIN_LIP_TYPE) ni {1 2 3}} {
  error "FIN_LIP_TYPE must be 1, 2, or 3."
}
set lip_type $::env(FIN_LIP_TYPE)
set resolution 2
if {[info exists ::env(FIN_RESOLUTION)]} {
  set resolution $::env(FIN_RESOLUTION)
}
set min_tile_density 0.20
if {[info exists ::env(FIN_MIN_TILE_DENSITY)]} {
  set min_tile_density $::env(FIN_MIN_TILE_DENSITY)
}
set results_group lip_lp_comparison
if {[info exists ::env(FIN_RESULTS_GROUP)]} {
  set results_group $::env(FIN_RESULTS_GROUP)
}
set results_tag "lip${lip_type}_r${resolution}"
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
  set rules [file normalize $::env(FIN_RULES)]
}

set fill_start [clock milliseconds]
set placed_fill [fixed_dissection_lp_lip_fill \
  -rules $rules \
  -window 50 \
  -origin {0 0} \
  -resolution $resolution \
  -min_tile_density $min_tile_density \
  -min_window_density 0.30 \
  -max_window_density 0.60 \
  -lip_type $lip_type]
puts "placed_fill_area=$placed_fill DBU^2"
puts [format "FILL_RUNTIME: lip=%s resolution=%s seconds=%.3f" $lip_type $resolution \
  [expr {([clock milliseconds] - $fill_start) / 1000.0}]]

set algorithms {alg2 alg3}
if {[info exists ::env(FIN_FLOATING_DENSITY_ALGORITHMS)]} {
  set algorithms $::env(FIN_FLOATING_DENSITY_ALGORITHMS)
}
foreach algorithm $algorithms {
  if {$algorithm ni {alg2 alg3}} {
    error "Unknown floating-density algorithm '$algorithm'; expected alg2 or alg3."
  }
  set profile_prefix "$results_dir/gcd_lip${lip_type}_${algorithm}_floating_density"
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

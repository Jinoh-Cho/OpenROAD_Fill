# Extract exact ALG2 floating-density profiles for the GCD benchmark.
set script_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $script_dir]
source "$test_dir/helpers.tcl"

read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$test_dir/prefill_bench/gcd_prefill.def"

set run_date [string trim [exec date +%Y%m%d]]
set results_group floating_density
if {[info exists ::env(FIN_RESULTS_GROUP)]} {
  set results_group $::env(FIN_RESULTS_GROUP)
}
set results_dir [file normalize "$test_dir/results/$run_date/gcd/$results_group"]
file mkdir $results_dir
set profile_tag gcd_floating_density
if {[info exists ::env(FIN_RESULTS_TAG)]} {
  set profile_tag $::env(FIN_RESULTS_TAG)
}
set profile_prefix "$results_dir/$profile_tag"
set rules "$test_dir/fill.json"
if {[info exists ::env(FIN_RULES)]} {
  set rules $::env(FIN_RULES)
}

set metal_area [tile_grid_metal_area \
  -rules $rules \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -min_window_density 0.30 \
  -max_density 0.60 \
  -floating_density_profile $profile_prefix]

if {$metal_area <= 0.0} {
  error "Floating-density analysis found no metal area."
}
puts "FLOATING_DENSITY_PROFILE_PREFIX=$profile_prefix"
puts "pass"
exit

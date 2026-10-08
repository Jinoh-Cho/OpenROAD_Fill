# Run one fill method on fresh AES prefill, then extract ALG2 profiles.
if {![info exists ::env(FIN_FILL_METHOD)]} {
  error "FIN_FILL_METHOD must be density, lp_minvar, or min_amount."
}
set method $::env(FIN_FILL_METHOD)
set script_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $script_dir]
source "$test_dir/helpers.tcl"
read_lef "$test_dir/sky130hd/sky130hd.tlef"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$test_dir/prefill_bench/aes_prefill.def"

set run_date [string trim [exec date +%Y%m%d]]
set results_dir [file normalize "$test_dir/results/$run_date/aes/fill_method_comparison/$method"]
file mkdir $results_dir
set rules "$test_dir/fill.json"
set fill_start [clock milliseconds]
switch -- $method {
  density { density_fill -rules $rules }
  lp_minvar {
    set placed_fill [fixed_dissection_lp_fill -rules $rules -window 50 \
      -origin {0 0} -resolution 2 -min_tile_density 0.20 \
      -min_window_density 0.30 -max_window_density 0.60]
    puts "placed_fill_area=$placed_fill DBU^2"
  }
  min_amount {
    set placed_fill [fixed_dissection_lp_min_amount_fill -rules $rules -window 50 \
      -origin {0 0} -resolution 2 -min_tile_density 0.20 \
      -min_window_density 0.30 -max_window_density 0.60]
    puts "placed_fill_area=$placed_fill DBU^2"
  }
  default { error "Unknown FIN_FILL_METHOD: $method" }
}
puts [format "FILL_RUNTIME: method=%s seconds=%.3f" $method \
  [expr {([clock milliseconds] - $fill_start) / 1000.0}]]
set profile_prefix "$results_dir/aes_${method}_floating_density"
tile_grid_metal_area -rules $rules -window 50 -origin {0 0} -resolution 2 \
  -min_window_density 0.30 -max_density 0.60 \
  -floating_density_profile $profile_prefix
puts "FLOATING_DENSITY_PROFILE_PREFIX=$profile_prefix"
puts "pass"
exit

# Place fill rectangles from the fixed-dissection LP tile targets in aes.
set script_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $script_dir]
source "$test_dir/helpers.tcl"

set flow_start [clock milliseconds]
proc checkpoint {flow_start message} {
  set elapsed [expr {([clock milliseconds] - $flow_start) / 1000.0}]
  puts [format "FIN-LP-STAGE +%.3fs: %s" $elapsed $message]
  flush stdout
}

checkpoint $flow_start "1/6 Reading Sky130 technology LEF"
read_lef "$test_dir/sky130hd/sky130hd.tlef"
checkpoint $flow_start "2/6 Reading Sky130 standard-cell LEF"
read_lef "$test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
checkpoint $flow_start "3/6 Reading routed aes DEF"
read_def "$test_dir/prefill_bench/aes_prefill.def"

set run_date [string trim [exec date +%Y%m%d]]
set results_dir [file normalize "$test_dir/results/$run_date/aes"]
file mkdir $results_dir
set svg_file "$results_dir/aes_fixed_dissection_lp_fill.svg"
set density_report "$results_dir/aes_fixed_dissection_lp_fill_density.json"
checkpoint $flow_start "4/6 Building LP tile targets, solving the LP, placing fill, and writing SVG"
if {[catch {
  set placed_fill [fixed_dissection_lp_fill \
    -rules "$test_dir/fill.json" \
    -window 50 \
    -origin {0 0} \
    -resolution 2 \
    -min_tile_density 0.20 \
    -min_window_density 0.30 \
    -max_window_density 0.60 \
    -svg $svg_file \
    -density_report $density_report]
} error options]} {
  checkpoint $flow_start "ERROR while building/solving/placing the fixed-dissection LP"
  puts "FIN-LP-ERROR: $error"
  return -options $options $error
}
checkpoint $flow_start "5/6 LP solve and fill placement completed"

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
checkpoint $flow_start "6/6 SVG outputs verified"
puts "pass"
exit

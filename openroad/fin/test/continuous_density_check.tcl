set fin_test_dir [file dirname [file normalize [info script]]]
source [file join $fin_test_dir helpers.tcl]
read_lef [file join $fin_test_dir sky130hd sky130hd.tlef]
read_lef [file join $fin_test_dir sky130hd sky130_fd_sc_hd_merged.lef]
read_def [file join $fin_test_dir gcd_prefill.def]

set report [make_result_file continuous_density_check.json]
set fills_before [llength [[ord::get_db_block] getFills]]
continuous_density_check -window_size 4 -layers {met2 met3} \
  -area {10 10 20 20} -min_density 0.20 -max_density 0.70 -report $report
if { [llength [[ord::get_db_block] getFills]] != $fills_before } {
  error "Continuous density checking must not change fills."
}
set stream [open $report r]
set json [read $stream]
close $stream
foreach expected {{"algorithm":"ALG3"} {"min_window_um":} {"max_window_um":} {"violations":}} {
  if { [string first $expected $json] < 0 } {
    error "Missing '$expected' in continuous report."
  }
}
foreach {args expected} {
  {} FIN-0032
  {-window_size 4 -area {10 10 20}} FIN-0035
  {-window_size 4 -area {10 10 20 20} -layers unknown_layer} FIN-0027
  {-window_size 0} {Window size must be positive}
  {-window_size 4 -min_density 0.8 -max_density 0.2} {Density bounds}
  {-window_size 4 -area {10 10 14 14}} FIN-0024
} {
  if { ![catch {continuous_density_check {*}$args} message]
    || [string first $expected $message] < 0 } {
    error "Expected '$expected'; got '$message'."
  }
}
continuous_density_check -window_size 4 -area {10 10 20 20}
puts "pass"

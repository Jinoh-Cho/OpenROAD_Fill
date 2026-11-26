set fin_test_dir [file dirname [file normalize [info script]]]
source [file join $fin_test_dir helpers.tcl]

read_lef [file join $fin_test_dir sky130hd sky130hd.tlef]
read_lef [file join $fin_test_dir sky130hd sky130_fd_sc_hd_merged.lef]
read_def [file join $fin_test_dir gcd_prefill.def]

set report [make_result_file discrete_density_check.json]
set fills_before [llength [[ord::get_db_block] getFills]]
tee -variable check_log [list discrete_density_check \
  -window_size 50 -resolution 4 -layers {met2 met3 met2} \
  -area {10 10 200 200} -min_density 0.10 -max_density 0.70 \
  -num_threads 2 -report $report]
if { [llength [[ord::get_db_block] getFills]] != $fills_before } {
  error "Density checking must not change fills."
}
if { ![file exists $report] || [file size $report] == 0 } {
  error "Missing density report."
}
set stream [open $report r]
set json [read $stream]
close $stream
foreach layer {met2 met3} {
  if { [string first "\"name\":\"$layer\"" $json] < 0 } {
    error "Missing $layer in density report."
  }
}
if { [string first {"name":"met1"} $json] >= 0 } {
  error "Unexpected layer in selected-layer report."
}
foreach {args expected} {
  {-window_size 50} FIN-0025
  {-window_size 50 -resolution 4 -layers unknown_layer} FIN-0027
  {-window_size 50 -resolution 4 -area {10 10 20}} FIN-0031
  {-window_size 50 -resolution 4 -num_threads 0} {thread count must be positive}
  {-window_size 50 -resolution 4 -min_density 0.8 -max_density 0.2} {Density bounds}
  {-window_size 50 -resolution 4 -area {10 10 10 100}} {positive width and height}
} {
  if { ![catch {discrete_density_check {*}$args} message]
    || [string first $expected $message] < 0 } {
    error "Expected '$expected'; got '$message'."
  }
}
discrete_density_check -window_size 50 -resolution 4
puts "pass"

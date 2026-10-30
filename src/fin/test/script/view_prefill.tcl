# Load a prefill benchmark with its SKY130 technology and cell LEFs.
# Default: GCD 80x80. Set FIN_INPUT_DEF to view another DEF.
set prefill_test_dir [file dirname [file dirname [file normalize [info script]]]]
set prefill_input_def "$prefill_test_dir/prefill_bench/expanded/gcd_prefill_80x80.def"
if {[info exists ::env(FIN_INPUT_DEF)]} {
  set prefill_input_def [file normalize $::env(FIN_INPUT_DEF)]
}
foreach prefill_file [list "$prefill_test_dir/sky130hd/sky130hd.tlef" \
                          "$prefill_test_dir/sky130hd/sky130_fd_sc_hd_merged.lef" \
                          $prefill_input_def] {
  if {![file isfile $prefill_file]} {
    error "Input file not found: $prefill_file"
  }
}
read_lef "$prefill_test_dir/sky130hd/sky130hd.tlef"
read_lef "$prefill_test_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def $prefill_input_def
if {[gui::enabled]} {
  gui::fit
}
puts "Loaded prefill DEF: $prefill_input_def"

set fin_test_dir [file dirname [file normalize [info script]]]
source [file join $fin_test_dir helpers.tcl]
set test_name lp_fill

read_lef [file join $fin_test_dir sky130hd sky130hd.tlef]
read_lef [file join $fin_test_dir sky130hd sky130_fd_sc_hd_merged.lef]
read_def [file join $fin_test_dir gcd_prefill.def]

foreach {window resolution expected} {
  50 0 {Resolution must be a positive integer.}
  50 -1 {Resolution must be a positive integer.}
  0 4 {Window size must be finite and positive.}
  50 3 {Window size in DBU must be divisible by resolution}
  50 100001 {Window size in DBU must be divisible by resolution}
} {
  if { ![catch {
    lp_fill -rules [file join $fin_test_dir fill.json] \
      -objective min_fill -window_size $window -resolution $resolution
  } message] || [string first $expected $message] < 0 } {
    error "Expected '$expected' for window=$window resolution=$resolution; got '$message'."
  }
}
if { ![catch {
  lp_fill -rules [file join $fin_test_dir fill.json] \
    -objective min_fill -window_size 50 -resolution 1.5
}] } {
  error "Fractional resolution must be rejected."
}

lp_fill -rules [file join $fin_test_dir fill.json] \
  -objective min_fill -solver glop -solver_threads 1 -num_threads 1 \
  -window_size 50 -resolution 4 \
  -min_density 0.10 -max_density 0.70 \
  -min_tile_density 0.10 -max_tile_density 0.70

set def_file [make_result_file $test_name.def]
write_def $def_file
if { [diff_files $def_file [file join $fin_test_dir $test_name.defok]] } {
  error "LP fill DEF differs from the reference result."
}
puts "pass"

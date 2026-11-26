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
  50 100001 {Resolution exceeds the window size in DBU}
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
foreach {requested adjusted resolution} {
  50 50.001 3
  50.002 50.001 3
  50.002 50.004 4
} {
  foreach size [list $requested $adjusted] name {requested adjusted} {
    odb::dbChip_destroy [odb::dbDatabase_getChip [ord::get_db]]
    read_def [file join $fin_test_dir gcd_prefill.def]
    lp_fill -rules [file join $fin_test_dir fill.json] \
      -objective min_fill -solver glop \
      -window_size $size -resolution $resolution \
      -min_density 0.10 -max_density 0.70 \
      -min_tile_density 0.10 -max_tile_density 0.70
    set ${name}_def [make_result_file ${test_name}_${requested}_${name}.def]
    write_def [set ${name}_def]
  }
  if { [diff_files $requested_def $adjusted_def] } {
    error "Automatically adjusted window $requested differs from explicit window $adjusted."
  }
}
puts "pass"

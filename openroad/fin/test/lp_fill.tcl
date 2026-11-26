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
  50 100001 {Resolution is too high for the window size on the manufacturing grid}
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
  50 49.995 3
  50.002 49.995 3
  50.002 50 4
} {
  foreach size [list $requested $adjusted] name {requested adjusted} {
    odb::dbChip_destroy [odb::dbDatabase_getChip [ord::get_db]]
    read_def [file join $fin_test_dir gcd_prefill.def]
    lp_fill -rules [file join $fin_test_dir fill.json] \
      -objective min_fill -solver glop \
      -window_size $size -resolution $resolution \
      -min_density 0.10 -max_density 0.70 \
      -min_tile_density 0.10 -max_tile_density 0.70
    set manufacturing_grid [[ord::get_db_tech] getManufacturingGrid]
    foreach fill [[ord::get_db_block] getFills] {
      set rect [$fill getRect]
      foreach coordinate [list [$rect xMin] [$rect yMin] [$rect xMax] [$rect yMax]] {
        if { $coordinate % $manufacturing_grid != 0 } {
          error "Fill coordinate $coordinate is off the manufacturing grid ($manufacturing_grid DBU)."
        }
      }
    }
    set ${name}_def [make_result_file ${test_name}_${requested}_${name}.def]
    write_def [set ${name}_def]
  }
  if { [diff_files $requested_def $adjusted_def] } {
    error "Automatically adjusted window $requested differs from explicit window $adjusted."
  }
}
odb::dbChip_destroy [odb::dbDatabase_getChip [ord::get_db]]
read_def [file join $fin_test_dir gcd_prefill.def]
set core [[ord::get_db_block] getCoreArea]
set dbu [[ord::get_db_tech] getDbUnitsPerMicron]
set manufacturing_grid [[ord::get_db_tech] getManufacturingGrid]
set short_side [expr {min([$core dx], [$core dy])}]
set quantum [expr {4 * $manufacturing_grid}]
set window_dbu [expr {$short_side / $quantum * $quantum}]
set window_um [expr {double($window_dbu) / $dbu}]
set fine_resolution [expr {$short_side / $manufacturing_grid}]
foreach {size resolution} [list $window_um 4 $window_um $fine_resolution 500 4] {
  if { ![catch {
    tee -variable error_log [list lp_fill \
      -rules [file join $fin_test_dir fill.json] \
      -objective min_fill -window_size $size -resolution $resolution \
      -min_density 0 -max_density 1]
  } message] || [string first "FIN-0024" $message] < 0 } {
    error "Expected FIN-0024 for window=$size resolution=$resolution; got '$message'."
  }
  foreach expected {
    {Requested window size:} {effective window size:} {core size:}
    {area } {um^2} {window positions: X=} {Reduce -window_size.}
  } {
    if { [string first $expected $error_log] < 0 } {
      error "Missing '$expected' in the invalid window error."
    }
  }
  if { [llength [[ord::get_db_block] getFills]] != 0 } {
    error "Invalid window configuration must not create fills."
  }
}
puts "pass"

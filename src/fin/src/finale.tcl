# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2025, The OpenROAD Authors

proc density_fill_debug { args } {
  fin::set_density_fill_debug_cmd
}

sta::define_cmd_args "density_fill" {[-rules rules_file]\
                                     [-area {lx ly ux uy}]}

proc density_fill { args } {
  sta::parse_key_args "density_fill" args \
    keys {-rules -area} flags {}

  if { [info exists keys(-rules)] } {
    set rules_file $keys(-rules)
  } else {
    utl::error FIN 7 "The -rules argument must be specified."
  }

  if { [info exists keys(-area)] } {
    set area $keys(-area)
    if { [llength $area] != 4 } {
      utl::error FIN 8 "The -area argument must be a list of 4 coordinates."
    }
    lassign $area lx ly ux uy
    set lx [ord::microns_to_dbu $lx]
    set ly [ord::microns_to_dbu $ly]
    set ux [ord::microns_to_dbu $ux]
    set uy [ord::microns_to_dbu $uy]
    set fill_area [odb::Rect x $lx $ly $ux $uy]
  } else {
    set fill_area [ord::get_db_core]
  }

  fin::density_fill_cmd $rules_file $fill_area
}

sta::define_cmd_args "density_fill_rectangle_extraction_benchmark" \
  {[-rules rules_file] [-area {lx ly ux uy}] [-left copies] [-right copies] [-runs runs]}

proc density_fill_rectangle_extraction_benchmark { args } {
  sta::parse_key_args "density_fill_rectangle_extraction_benchmark" args \
    keys {-rules -area -left -right -runs} flags {}

  if { ![info exists keys(-rules)] } {
    utl::error FIN 13 "The -rules argument must be specified."
  }
  set rules_file $keys(-rules)

  if { [info exists keys(-area)] } {
    set area $keys(-area)
    if { [llength $area] != 4 } {
      utl::error FIN 14 "The -area argument must be a list of 4 coordinates."
    }
    lassign $area lx ly ux uy
    set fill_area [odb::Rect x [ord::microns_to_dbu $lx] [ord::microns_to_dbu $ly] \
                           [ord::microns_to_dbu $ux] [ord::microns_to_dbu $uy]]
  } else {
    set fill_area [ord::get_db_core]
  }

  foreach {option value} {-left 0 -right 0 -runs 1} {
    if { [info exists keys($option)] } {
      set value $keys($option)
    }
    if { ![string is integer -strict $value] || $value < 0 } {
      utl::error FIN 15 "$option must be a non-negative integer."
    }
    set [string range $option 1 end] $value
  }
  if { $runs == 0 } {
    utl::error FIN 16 "-runs must be greater than zero."
  }

  fin::density_fill_rectangle_extraction_benchmark_cmd \
    $rules_file $fill_area $left $right $runs
}

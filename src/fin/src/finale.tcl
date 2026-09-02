# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2025, The OpenROAD Authors

proc density_fill_debug { args } {
  fin::set_density_fill_debug_cmd
}

proc min_var_fill_debug { args } {
  fin::set_min_var_fill_debug_cmd
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

sta::define_cmd_args "min_var_fill" {[-rules rules_file]\
                                     [-area {lx ly ux uy}]}

proc min_var_fill { args } {
  sta::parse_key_args "min_var_fill" args \
    keys {-rules -area} flags {}

  if { [info exists keys(-rules)] } {
    set rules_file $keys(-rules)
  } else {
    utl::error FIN 28 "The -rules argument must be specified."
  }

  if { [info exists keys(-area)] } {
    set area $keys(-area)
    if { [llength $area] != 4 } {
      utl::error FIN 29 "The -area argument must be a list of 4 coordinates."
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

  fin::min_var_fill_cmd $rules_file $fill_area
}

sta::define_cmd_args "density_fill_rectangle_extraction_benchmark" \
  {[-rules rules_file] [-area {lx ly ux uy}] [-left copies] [-right copies] [-bottom copies] [-top copies] [-runs runs]}

proc density_fill_rectangle_extraction_benchmark { args } {
  sta::parse_key_args "density_fill_rectangle_extraction_benchmark" args \
    keys {-rules -area -left -right -bottom -top -runs} flags {}

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

  foreach {option value} {-left 0 -right 0 -bottom 0 -top 0 -runs 1} {
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
    $rules_file $fill_area $left $right $bottom $top $runs
}

sta::define_cmd_args "tile_grid_metal_area" \
  {[-rules rules_file] [-area {lx ly ux uy}] -window window_size [-origin {x y}] [-resolution resolution] [-svg file]}

proc tile_grid_metal_area { args } {
  sta::parse_key_args "tile_grid_metal_area" args \
    keys {-rules -area -window -origin -resolution -svg} flags {}
  if { ![info exists keys(-rules)] || ![info exists keys(-window)] } {
    utl::error FIN 18 "The -rules and -window arguments must be specified."
  }
  set region [ord::get_db_core]
  if { [info exists keys(-area)] } {
    lassign $keys(-area) lx ly ux uy
    set region [odb::Rect x [ord::microns_to_dbu $lx] [ord::microns_to_dbu $ly] \
                           [ord::microns_to_dbu $ux] [ord::microns_to_dbu $uy]]
  }
  set origin {0 0}
  if { [info exists keys(-origin)] } { set origin $keys(-origin) }
  lassign $origin ox oy
  # Interpret -origin as an offset from the lower-left corner of the region.
  set origin_x [expr {[$region xMin] + [ord::microns_to_dbu $ox]}]
  set origin_y [expr {[$region yMin] + [ord::microns_to_dbu $oy]}]
  set resolution 4
  if { [info exists keys(-resolution)] } { set resolution $keys(-resolution) }
  set svg_file ""
  if { [info exists keys(-svg)] } { set svg_file $keys(-svg) }
  return [fin::tile_grid_metal_area_cmd $keys(-rules) $region \
    [odb::Point x $origin_x $origin_y] \
    [ord::microns_to_dbu $keys(-window)] $resolution $svg_file]
}

sta::define_cmd_args "fixed_dissection_lp" \
  {[-rules rules_file] [-area {lx ly ux uy}] -window window_size [-origin {x y}] [-resolution resolution] -max_density density [-svg file]}

proc fixed_dissection_lp { args } {
  sta::parse_key_args "fixed_dissection_lp" args \
    keys {-rules -area -window -origin -resolution -max_density -svg} flags {}
  foreach required {-rules -window -max_density} {
    if { ![info exists keys($required)] } {
      utl::error FIN 32 "The $required argument must be specified."
    }
  }
  if { ![string is double -strict $keys(-max_density)] \
       || $keys(-max_density) < 0.0 || $keys(-max_density) > 1.0 } {
    utl::error FIN 33 "The -max_density argument must be between 0.0 and 1.0."
  }

  set region [ord::get_db_core]
  if { [info exists keys(-area)] } {
    set area $keys(-area)
    if { [llength $area] != 4 } {
      utl::error FIN 34 "The -area argument must be a list of 4 coordinates."
    }
    lassign $area lx ly ux uy
    set region [odb::Rect x [ord::microns_to_dbu $lx] [ord::microns_to_dbu $ly] \
                           [ord::microns_to_dbu $ux] [ord::microns_to_dbu $uy]]
  }
  set origin {0 0}
  if { [info exists keys(-origin)] } { set origin $keys(-origin) }
  if { [llength $origin] != 2 } {
    utl::error FIN 35 "The -origin argument must be a list of 2 coordinates."
  }
  lassign $origin ox oy
  set origin_x [expr {[$region xMin] + [ord::microns_to_dbu $ox]}]
  set origin_y [expr {[$region yMin] + [ord::microns_to_dbu $oy]}]
  set resolution 4
  if { [info exists keys(-resolution)] } { set resolution $keys(-resolution) }
  set svg_file ""
  if { [info exists keys(-svg)] } { set svg_file $keys(-svg) }
  return [fin::fixed_dissection_lp_cmd $keys(-rules) $region \
    [odb::Point x $origin_x $origin_y] \
    [ord::microns_to_dbu $keys(-window)] $resolution $keys(-max_density) \
    $svg_file]
}

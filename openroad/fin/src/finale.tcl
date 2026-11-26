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

sta::define_cmd_args "lp_fill" {
  -rules rules_file
  -objective min_fill|min_var
  -window_size size
  -resolution tiles_per_side
  [-solver glop|pdlp]
  [-solver_threads threads]
  [-num_threads threads]
  [-min_density density]
  [-max_density density]
  [-min_tile_density density]
  [-max_tile_density density]
}

proc lp_fill { args } {
  sta::parse_key_args "lp_fill" args \
    keys {-rules -objective -solver -solver_threads -num_threads
      -window_size -resolution -min_density -max_density
      -min_tile_density -max_tile_density} flags {}
  sta::check_argc_eq0 "lp_fill" $args

  foreach key {-rules -objective -window_size -resolution} {
    if { ![info exists keys($key)] } {
      utl::error FIN 17 "The $key argument must be specified."
    }
  }
  foreach {key value} {
    -solver glop -solver_threads 1 -num_threads 1
    -min_density 0.20 -max_density 0.70
    -min_tile_density 0.0 -max_tile_density 1.0
  } {
    if { ![info exists keys($key)] } {
      set keys($key) $value
    }
  }
  fin::lp_fill_cmd $keys(-rules) $keys(-objective) $keys(-solver) \
    $keys(-solver_threads) $keys(-num_threads) \
    $keys(-window_size) $keys(-resolution) \
    $keys(-min_density) $keys(-max_density) \
    $keys(-min_tile_density) $keys(-max_tile_density)
}

sta::define_cmd_args "discrete_density_check" {
  -window_size size
  -resolution tiles_per_side
  [-layers layer_names]
  [-area {lx ly ux uy}]
  [-min_density density]
  [-max_density density]
  [-num_threads threads]
  [-report filename]
}

proc discrete_density_check { args } {
  sta::parse_key_args "discrete_density_check" args \
    keys {-window_size -resolution -layers -area -min_density -max_density
      -num_threads -report} flags {}
  sta::check_argc_eq0 "discrete_density_check" $args
  foreach key {-window_size -resolution} {
    if { ![info exists keys($key)] } {
      utl::error FIN 25 "The $key argument must be specified."
    }
  }
  foreach {key value} {
    -layers {} -min_density 0.20 -max_density 0.70 -num_threads 1 -report {}
  } {
    if { ![info exists keys($key)] } {
      set keys($key) $value
    }
  }
  set has_area [info exists keys(-area)]
  set area [odb::Rect]
  if { $has_area } {
    if { [llength $keys(-area)] != 4 } {
      utl::error FIN 31 "The -area argument must be a list of 4 coordinates in microns."
    }
    lassign $keys(-area) lx ly ux uy
    $area init [ord::microns_to_dbu $lx] [ord::microns_to_dbu $ly] \
      [ord::microns_to_dbu $ux] [ord::microns_to_dbu $uy]
  }
  fin::discrete_density_check_cmd $keys(-window_size) $keys(-resolution) \
    $keys(-num_threads) $keys(-layers) $has_area $area \
    $keys(-min_density) $keys(-max_density) $keys(-report)
}

sta::define_cmd_args "continuous_density_check" {
  -window_size size
  [-layers layer_names]
  [-area {lx ly ux uy}]
  [-min_density density]
  [-max_density density]
  [-report filename]
}

proc continuous_density_check { args } {
  sta::parse_key_args "continuous_density_check" args \
    keys {-window_size -layers -area -min_density -max_density -report} flags {}
  sta::check_argc_eq0 "continuous_density_check" $args
  if { ![info exists keys(-window_size)] } {
    utl::error FIN 32 "The -window_size argument must be specified."
  }
  foreach {key value} {-layers {} -min_density 0.20 -max_density 0.70 -report {}} {
    if { ![info exists keys($key)] } {
      set keys($key) $value
    }
  }
  set has_area [info exists keys(-area)]
  set area [odb::Rect]
  if { $has_area } {
    if { [llength $keys(-area)] != 4 } {
      utl::error FIN 35 "The -area argument must be a list of 4 coordinates in microns."
    }
    lassign $keys(-area) lx ly ux uy
    $area init [ord::microns_to_dbu $lx] [ord::microns_to_dbu $ly] \
      [ord::microns_to_dbu $ux] [ord::microns_to_dbu $uy]
  }
  fin::continuous_density_check_cmd $keys(-window_size) $keys(-layers) \
    $has_area $area $keys(-min_density) $keys(-max_density) $keys(-report)
}

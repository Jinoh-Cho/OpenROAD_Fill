# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020-2025, The OpenROAD Authors

# Internal option conversion shared by the public FIN commands.
namespace eval fin {}

proc fin::get_option { keys_var option default } {
  upvar 1 $keys_var options
  if { [info exists options($option)] } { return $options($option) }
  return $default
}

proc fin::get_fill_region { keys_var area_error } {
  upvar 1 $keys_var options
  if { ![info exists options(-area)] } { return [ord::get_db_core] }
  if { [llength $options(-area)] != 4 } {
    utl::error FIN $area_error "The -area argument must be a list of 4 coordinates."
  }
  set coordinates [lmap coordinate $options(-area) {
    ord::microns_to_dbu $coordinate
  }]
  return [odb::Rect x {*}$coordinates]
}

proc fin::get_grid_parameters { keys_var area_error origin_error } {
  upvar 1 $keys_var options
  set region [fin::get_fill_region options $area_error]
  set offset [fin::get_option options -origin {0 0}]
  if { [llength $offset] != 2 } {
    utl::error FIN $origin_error "The -origin argument must be a list of 2 coordinates."
  }
  lassign $offset ox oy
  # Interpret -origin as an offset from the lower-left corner of the region.
  set origin_x [expr {[$region xMin] + [ord::microns_to_dbu $ox]}]
  set origin_y [expr {[$region yMin] + [ord::microns_to_dbu $oy]}]
  return [list $region [odb::Point x $origin_x $origin_y] \
    [ord::microns_to_dbu $options(-window)] \
    [fin::get_option options -resolution 4]]
}

proc fin::get_density_option { keys_var option default error_id } {
  upvar 1 $keys_var options
  if { ![info exists options($option)] } { return $default }
  set value $options($option)
  if { ![string is double -strict $value] || $value < 0.0 || $value > 1.0 } {
    utl::error FIN $error_id "The $option argument must be between 0.0 and 1.0."
  }
  return $value
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

  set fill_area [fin::get_fill_region keys 8]

  fin::density_fill_cmd $rules_file $fill_area
}

sta::define_cmd_args "tile_grid_metal_area" \
  {[-rules rules_file] [-area {lx ly ux uy}] -window window_size [-origin {x y}] [-resolution resolution] [-min_window_density density] [-max_density density] [-svg file] [-density_report file] [-floating_density_profile file] [-floating_density_algorithm alg2|alg3]}

proc tile_grid_metal_area { args } {
  sta::parse_key_args "tile_grid_metal_area" args \
    keys {-rules -area -window -origin -resolution -min_window_density -max_density -svg -density_report -floating_density_profile -floating_density_algorithm} flags {}
  if { ![info exists keys(-rules)] || ![info exists keys(-window)] } {
    utl::error FIN 18 "The -rules and -window arguments must be specified."
  }
  lassign [fin::get_grid_parameters keys 68 69] region origin window resolution
  set min_window_density [fin::get_density_option keys -min_window_density 0.0 64]
  set max_density [fin::get_density_option keys -max_density -1.0 60]
  set svg_file [fin::get_option keys -svg ""]
  set density_report_file [fin::get_option keys -density_report ""]
  set floating_density_profile_file [fin::get_option keys -floating_density_profile ""]
  set floating_density_algorithm [fin::get_option keys -floating_density_algorithm alg3]
  if { $floating_density_algorithm ni {alg2 alg3} } {
    utl::error FIN 67 "-floating_density_algorithm must be alg2 or alg3."
  }
  return [fin::tile_grid_metal_area_cmd $keys(-rules) $region \
    $origin \
    $window $resolution $min_window_density $max_density $svg_file $density_report_file $floating_density_profile_file $floating_density_algorithm]
}

sta::define_cmd_args "fixed_dissection_lp_min_var_fill" \
  {[-rules rules_file] [-area {lx ly ux uy}] -window window_size [-origin {x y}] [-resolution resolution] [-min_tile_density density] [-max_tile_density density] [-min_window_density density] [-max_window_density density] [-svg file] [-density_report file]}

sta::define_cmd_args "fixed_dissection_lp_min_amount_fill" \
  {[-rules rules_file] [-area {lx ly ux uy}] -window window_size [-origin {x y}] [-resolution resolution] [-min_tile_density density] [-max_tile_density density] [-min_window_density density] [-max_window_density density] [-svg file] [-density_report file]}

sta::define_cmd_args "fixed_dissection_lp_lip_fill" \
  {[-rules rules_file] [-area {lx ly ux uy}] -window window_size -lip_type {1|2|3} [-origin {x y}] [-resolution resolution] [-min_tile_density density] [-max_tile_density density] [-min_window_density density] [-max_window_density density] [-svg file] [-density_report file]}

proc fin::lp_fill_impl { command_name keys_var } {
  upvar 1 $keys_var keys
  if { $command_name eq "fin::fixed_dissection_lp_lip_fill_cmd" \
       && (![info exists keys(-lip_type)] || $keys(-lip_type) ni {1 2 3}) } {
    utl::error FIN 64 "The -lip_type argument must be 1, 2, or 3."
  }
  foreach required {-rules -window} {
    if { ![info exists keys($required)] } {
      utl::error FIN 42 "The $required argument must be specified."
    }
  }
  set max_window_density [fin::get_density_option keys -max_window_density 1.0 43]
  set min_tile_density [fin::get_density_option keys -min_tile_density 0.0 49]
  set max_tile_density [fin::get_density_option keys -max_tile_density 1.0 62]
  if { $min_tile_density > $max_tile_density } {
    utl::error FIN 63 "The -min_tile_density argument cannot exceed -max_tile_density."
  }
  set min_window_density [fin::get_density_option keys -min_window_density 0.0 61]
  lassign [fin::get_grid_parameters keys 44 45] region origin window resolution
  set svg_file [fin::get_option keys -svg ""]
  set density_report_file [fin::get_option keys -density_report ""]
  if { $command_name eq "fin::fixed_dissection_lp_lip_fill_cmd" } {
    return [$command_name $keys(-rules) $region \
      $origin \
      $window $resolution $min_tile_density \
      $max_tile_density $min_window_density $max_window_density \
      $keys(-lip_type) $svg_file $density_report_file]
  }
  return [{*}$command_name $keys(-rules) $region \
    $origin \
    $window $resolution $min_tile_density $max_tile_density $min_window_density $max_window_density \
    $svg_file $density_report_file]
}

proc fixed_dissection_lp_min_var_fill { args } {
  sta::parse_key_args "fixed_dissection_lp_min_var_fill" args \
    keys {-rules -area -window -origin -resolution -min_tile_density -max_tile_density -min_window_density -max_window_density -svg -density_report -lip_type} flags {}
  return [fin::lp_fill_impl fin::fixed_dissection_lp_min_var_fill_cmd keys]
}

proc fixed_dissection_lp_min_amount_fill { args } {
  sta::parse_key_args "fixed_dissection_lp_min_amount_fill" args \
    keys {-rules -area -window -origin -resolution -min_tile_density -max_tile_density -min_window_density -max_window_density -svg -density_report -lip_type} flags {}
  return [fin::lp_fill_impl fin::fixed_dissection_lp_min_amount_fill_cmd keys]
}

proc fixed_dissection_lp_lip_fill { args } {
  sta::parse_key_args "fixed_dissection_lp_lip_fill" args \
    keys {-rules -area -window -origin -resolution -min_tile_density -max_tile_density -min_window_density -max_window_density -svg -density_report -lip_type} flags {}
  return [fin::lp_fill_impl fin::fixed_dissection_lp_lip_fill_cmd keys]
}

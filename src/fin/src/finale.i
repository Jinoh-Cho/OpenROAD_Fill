// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

%{
#include "fin/Finale.h"
#include "ord/OpenRoad.hh"

namespace {
fin::Finale* getFinale()
{
  return ord::OpenRoad::openRoad()->getFinale();
}
}  // namespace

%}

%include "../../Exception.i"

%inline %{

  void density_fill_cmd(const char* rules_filename, const odb::Rect& fill_area)
  {
    getFinale()->densityFill(rules_filename, fill_area);
  }

  double tile_grid_metal_area_cmd(const char* rules_filename,
                                  const odb::Rect& region,
                                  const odb::Point& origin,
                                  int window_size,
                                  int resolution,
                                  double min_window_density,
                                  double max_density,
                                  const char* svg_filename,
                                  const char* density_report_filename,
                                  const char* floating_density_profile_filename,
                                  const char* floating_density_algorithm)
  {
    return getFinale()->tileGridMetalArea(rules_filename,
                                     region,
                                     origin,
                                     window_size,
                                     resolution,
                                     min_window_density,
                                     max_density,
                                     svg_filename,
                                     density_report_filename,
                                     floating_density_profile_filename,
                                     floating_density_algorithm);
  }

  double fixed_dissection_lp_min_var_fill_cmd(const char* rules_filename,
                                      const odb::Rect& region,
                                      const odb::Point& origin,
                                      int window_size,
                                      int resolution,
                                      double min_tile_density,
                                      double max_tile_density,
                                      double min_window_density,
                                      double max_window_density,
                                      const char* svg_filename,
                                      const char* density_report_filename)
  {
    return getFinale()->fixedDissectionLpMinVarFill(rules_filename,
                                         region,
                                         origin,
                                         window_size,
                                         resolution,
                                         min_tile_density,
                                         max_tile_density,
                                         min_window_density,
                                         max_window_density,
                                         svg_filename,
                                         density_report_filename);
  }

  double fixed_dissection_lp_min_amount_fill_cmd(
      const char* rules_filename,
      const odb::Rect& region,
      const odb::Point& origin,
      int window_size,
      int resolution,
      double min_tile_density,
      double max_tile_density,
      double min_window_density,
      double max_window_density,
      const char* svg_filename,
      const char* density_report_filename)
  {
    return getFinale()->fixedDissectionLpMinAmountFill(rules_filename,
                                                  region,
                                                  origin,
                                                  window_size,
                                                  resolution,
                                                  min_tile_density,
                                                  max_tile_density,
                                                  min_window_density,
                                                  max_window_density,
                                                  svg_filename,
                                                  density_report_filename);
  }

  double fixed_dissection_lp_lip_fill_cmd(const char* rules_filename,
                                          const odb::Rect& region,
                                          const odb::Point& origin,
                                          int window_size,
                                          int resolution,
                                          double min_tile_density,
                                          double max_tile_density,
                                          double min_window_density,
                                          double max_window_density,
                                          int lip_type,
                                          const char* svg_filename,
                                          const char* density_report_filename)
  {
    return getFinale()->fixedDissectionLpLipFill(rules_filename,
                                            region,
                                            origin,
                                            window_size,
                                            resolution,
                                            min_tile_density,
                                            max_tile_density,
                                            min_window_density,
                                            max_window_density,
                                            lip_type,
                                            svg_filename,
                                            density_report_filename);
  }

%} // inline

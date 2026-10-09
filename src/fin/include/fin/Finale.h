// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#pragma once

#include "odb/geom.h"

namespace odb {
class dbDatabase;
}
namespace utl {
class Logger;
}

namespace fin {

////////////////////////////////////////////////////////////////

class Finale
{
 public:
  Finale(odb::dbDatabase* db, utl::Logger* logger);

  void densityFill(const char* rules_filename, const odb::Rect& fill_area);

  double tileGridMetalArea(const char* rules_filename,
                           const odb::Rect& region,
                           const odb::Point& origin,
                           int window_size,
                           int resolution,
                           double min_window_density,
                           double max_density,
                           const char* svg_filename,
                           const char* density_report_filename,
                           const char* floating_density_profile_filename,
                           const char* floating_density_algorithm);

  double fixedDissectionLpMinVarFill(const char* rules_filename,
                                     const odb::Rect& region,
                                     const odb::Point& origin,
                                     int window_size,
                                     int resolution,
                                     double min_tile_density,
                                     double max_tile_density,
                                     double min_window_density,
                                     double max_window_density,
                                     const char* svg_filename,
                                     const char* density_report_filename);
  double fixedDissectionLpMinAmountFill(const char* rules_filename,
                                        const odb::Rect& region,
                                        const odb::Point& origin,
                                        int window_size,
                                        int resolution,
                                        double min_tile_density,
                                        double max_tile_density,
                                        double min_window_density,
                                        double max_window_density,
                                        const char* svg_filename,
                                        const char* density_report_filename);
  double fixedDissectionLpLipFill(const char* rules_filename,
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
                                  const char* density_report_filename);

 private:
  odb::dbDatabase* db_ = nullptr;
  utl::Logger* logger_ = nullptr;
};

}  // namespace fin

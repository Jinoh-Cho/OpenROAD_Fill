// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#include "fin/Finale.h"

#include <stdexcept>

#include "DensityFill.h"
#include "LPFill.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

////////////////////////////////////////////////////////////////

Finale::Finale(odb::dbDatabase* db, utl::Logger* logger)
    : db_(db), logger_(logger)
{
}

void Finale::densityFill(const char* rules_filename, const odb::Rect& fill_area)
{
  DensityFill filler(db_, logger_, false);
  filler.fill(rules_filename, fill_area);
}

double Finale::tileGridMetalArea(const char* rules_filename,
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
  LPFill filler(db_, logger_);
  return filler.tileGridMetalArea(rules_filename,
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

double Finale::fixedDissectionLpMinVarFill(const char* rules_filename,
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
  LPFill filler(db_, logger_);
  return filler.fillLp(rules_filename,
                       region,
                       origin,
                       window_size,
                       resolution,
                       min_tile_density,
                       max_tile_density,
                       min_window_density,
                       max_window_density,
                       svg_filename,
                       density_report_filename,
                       FillMethod::MinVar);
}

double Finale::fixedDissectionLpMinAmountFill(
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
  LPFill filler(db_, logger_);
  return filler.fillLp(rules_filename,
                       region,
                       origin,
                       window_size,
                       resolution,
                       min_tile_density,
                       max_tile_density,
                       min_window_density,
                       max_window_density,
                       svg_filename,
                       density_report_filename,
                       FillMethod::MinFillAmount);
}

double Finale::fixedDissectionLpLipFill(const char* rules_filename,
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
  if (lip_type < 1 || lip_type > 3) {
    throw std::invalid_argument("Lip LP type must be 1, 2, or 3.");
  }
  constexpr FillMethod methods[]{
      FillMethod::Lip1, FillMethod::Lip2, FillMethod::Lip3};
  LPFill filler(db_, logger_);
  return filler.fillLp(rules_filename,
                       region,
                       origin,
                       window_size,
                       resolution,
                       min_tile_density,
                       max_tile_density,
                       min_window_density,
                       max_window_density,
                       svg_filename,
                       density_report_filename,
                       methods[lip_type - 1]);
}

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#include "fin/Finale.h"

#include "DensityFill.h"
#include "MinVarFill.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

////////////////////////////////////////////////////////////////

Finale::Finale(odb::dbDatabase* db, utl::Logger* logger)
    : db_(db), logger_(logger)
{
}

void Finale::setDebug()
{
  debug_ = true;
}

void Finale::setMinVarDebug()
{
  min_var_debug_ = true;
}

void Finale::densityFill(const char* rules_filename, const odb::Rect& fill_area)
{
  DensityFill filler(db_, logger_, debug_);
  filler.fill(rules_filename, fill_area);
}

void Finale::minVarFill(const char* rules_filename, const odb::Rect& fill_area)
{
  MinVarFill filler(db_, logger_, min_var_debug_);
  filler.fill(rules_filename, fill_area);
}

void Finale::benchmarkRectangleExtraction(const char* rules_filename,
                                          const odb::Rect& fill_area,
                                          int left_copies,
                                          int right_copies,
                                          int bottom_copies,
                                          int top_copies,
                                          int runs)
{
  MinVarFill filler(db_, logger_);
  filler.benchmarkRectangleExtraction(rules_filename,
                                      fill_area,
                                      left_copies,
                                      right_copies,
                                      bottom_copies,
                                      top_copies,
                                      runs);
}

double Finale::tileGridMetalArea(const char* rules_filename,
                                 const odb::Rect& region,
                                 const odb::Point& origin,
                                 int window_size,
                                 int resolution,
                                 const char* svg_filename)
{
  MinVarFill filler(db_, logger_);
  return filler.tileGridMetalArea(
      rules_filename, region, origin, window_size, resolution, svg_filename);
}

double Finale::fixedDissectionLp(const char* rules_filename,
                                 const odb::Rect& region,
                                 const odb::Point& origin,
                                 int window_size,
                                 int resolution,
                                 double max_density,
                                 const char* svg_filename)
{
  MinVarFill filler(db_, logger_);
  return filler.fixedDissectionLp(rules_filename,
                                  region,
                                  origin,
                                  window_size,
                                  resolution,
                                  max_density,
                                  svg_filename);
}

}  // namespace fin

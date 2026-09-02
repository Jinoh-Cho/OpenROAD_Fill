// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#pragma once

#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

////////////////////////////////////////////////////////////////

class Finale
{
 public:
  Finale(odb::dbDatabase* db, utl::Logger* logger);

  void densityFill(const char* rules_filename, const odb::Rect& fill_area);
  void minVarFill(const char* rules_filename, const odb::Rect& fill_area);
  void benchmarkRectangleExtraction(const char* rules_filename,
                                    const odb::Rect& fill_area,
                                    int left_copies,
                                    int right_copies,
                                    int bottom_copies,
                                    int top_copies,
                                    int runs);
  double tileGridMetalArea(const char* rules_filename,
                           const odb::Rect& region,
                           const odb::Point& origin,
                           int window_size,
                           int resolution,
                           const char* svg_filename);
  double fixedDissectionLp(const char* rules_filename,
                           const odb::Rect& region,
                           const odb::Point& origin,
                           int window_size,
                           int resolution,
                           double max_density,
                           const char* svg_filename);

  void setDebug();
  void setMinVarDebug();

 private:
  odb::dbDatabase* db_ = nullptr;
  utl::Logger* logger_ = nullptr;
  bool debug_ = false;
  bool min_var_debug_ = false;
};

}  // namespace fin

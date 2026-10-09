// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <memory>

#include "FillConfig.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

class Graphics;

enum class FillMethod
{
  MinVar,
  MinFillAmount,
  Lip1,
  Lip2,
  Lip3
};

// Coordinates candidate generation, LP planners, and physical placement.
class LPFill
{
 public:
  LPFill(odb::dbDatabase* db, utl::Logger* logger, bool debug = false);
  ~LPFill();

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

  double fillLp(const char* rules_filename,
                const odb::Rect& region,
                const odb::Point& origin,
                int window_size,
                int resolution,
                double min_tile_density,
                double max_tile_density,
                double min_window_density,
                double max_window_density,
                const char* svg_filename,
                const char* density_report_filename,
                FillMethod method);

 private:
  odb::dbDatabase* db_;
  utl::Logger* logger_;
  std::unique_ptr<Graphics> graphics_;
};

}  // namespace fin

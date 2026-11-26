// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

struct LPFillOptions
{
  std::string rules_file;
  std::string objective = "min_fill";
  std::string solver = "glop";
  int solver_threads = 1;
  int num_threads = 1;
  double window_size = 50.0;
  int resolution = 4;
  double min_density = 0.2;
  double max_density = 0.7;
  double min_tile_density = 0.0;
  double max_tile_density = 1.0;
};

struct DiscreteDensityCheckOptions
{
  double window_size = 50.0;
  int resolution = 4;
  int num_threads = 1;
  double min_density = 0.2;
  double max_density = 0.7;
  std::vector<std::string> layers;
  bool has_area = false;
  odb::Rect area;
  std::string report;
};

class Finale
{
 public:
  Finale(odb::dbDatabase* db, utl::Logger* logger);

  void densityFill(const char* rules_filename, const odb::Rect& fill_area);

  void lpFill(const LPFillOptions& options);

  void discreteDensityCheck(const DiscreteDensityCheckOptions& options);

  void setDebug();

 private:
  odb::dbDatabase* db_ = nullptr;
  utl::Logger* logger_ = nullptr;
  bool debug_ = false;
};

}  // namespace fin

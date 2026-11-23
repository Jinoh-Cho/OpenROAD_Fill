// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#pragma once

#include <string>

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
  double window_width = 50.0;
  double window_height = 50.0;
  double step_x = 12.5;
  double step_y = 12.5;
  double min_density = 0.2;
  double max_density = 0.7;
  std::string placement_strategy = "sequential";
};

class Finale
{
 public:
  Finale(odb::dbDatabase* db, utl::Logger* logger);

  void densityFill(const char* rules_filename, const odb::Rect& fill_area);

  void lpFill(const LPFillOptions& options);

  void setDebug();

 private:
  odb::dbDatabase* db_ = nullptr;
  utl::Logger* logger_ = nullptr;
  bool debug_ = false;
};

}  // namespace fin

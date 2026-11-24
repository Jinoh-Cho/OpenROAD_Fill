// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "FillCheck.h"
#include "fin/Finale.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace fin {

// Numeric LP input is independent of the database and rectangle placement.
struct FillLPProblem
{
  std::vector<double> tile_areas;
  std::vector<double> metal_areas;
  std::vector<double> capacities;
  std::vector<FillWindow> windows;
};

struct RelaxedWindowMinimum
{
  size_t window_index;
  double effective_density;
  double requested_fill_area;
  double available_fill_area;
};

struct FillLPSolution
{
  bool solved = false;
  std::vector<double> fill_areas;
  size_t relaxed_windows = 0;
  size_t relaxed_tiles = 0;
  std::vector<RelaxedWindowMinimum> relaxed_minimums;
  int64_t iterations = -1;
  std::string status;
};

void validateLPFillOptions(const LPFillOptions& options);
FillLPSolution solveFillLP(const FillLPProblem& problem,
                           const LPFillOptions& options);

class LPFill
{
 public:
  LPFill(odb::dbDatabase* db, utl::Logger* logger);
  void fill(const LPFillOptions& options);

 private:
  void fillLayer(odb::dbBlock* block,
                 odb::dbTechLayer* layer,
                 const FillGrid& grid,
                 const struct FillLayerConfig& config,
                 const LPFillOptions& options,
                 std::vector<odb::dbFill*>& created);

  odb::dbDatabase* db_;
  utl::Logger* logger_;
};

}  // namespace fin

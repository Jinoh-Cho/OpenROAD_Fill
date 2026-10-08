// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstddef>
#include <vector>

namespace fin {

// J40 fixed-dissection LP input.  All areas use the same unit (normally
// DBU^2).  A window contains indices into the three per-tile vectors.
struct FixedDissectionLpProblem
{
  double max_window_density = 1.0;
  double min_tile_density = 0.0;
  double max_tile_density = 1.0;
  double min_window_density = 0.0;
  std::vector<double> tile_areas;
  std::vector<double> feature_areas;
  std::vector<double> max_fill_areas;
  std::vector<std::vector<size_t>> windows;
};

struct FixedDissectionLpResult
{
  bool solved = false;
  double min_window_area = 0.0;
  double lip_value = 0.0;
  std::vector<double> fill_areas;
};

enum class FixedDissectionLpObjective
{
  kMaximizeMinimumWindowArea,
  kMinimizeTotalFillArea
};

// Solve J40 equations (2)-(5): maximize the minimum post-fill window area,
// subject to tile fill capacities and density bounds in every window.
// Throws std::invalid_argument when the problem dimensions are inconsistent.
FixedDissectionLpResult solveFixedDissectionLp(
    const FixedDissectionLpProblem& problem,
    FixedDissectionLpObjective objective
    = FixedDissectionLpObjective::kMaximizeMinimumWindowArea);

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fin {

enum class FillLpSolver
{
  Glop,
  Pdlp
};

inline const char* fillLpSolverName(FillLpSolver solver)
{
  switch (solver) {
    case FillLpSolver::Glop:
      return "GLOP";
    case FillLpSolver::Pdlp:
      return "PDLP";
  }
  throw std::invalid_argument("Unknown LP solver.");
}

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

  FillLpSolver lp_solver = FillLpSolver::Glop;
  int lp_threads = 1;
  std::string diagnostics_prefix = "";
  // Enable backend convergence logging without changing solver tolerances.
  bool solver_log = false;
  // Zero preserves backend defaults; a positive value overrides PDLP dual
  // absolute/relative tolerances, keeping primal and gap at 1e-6.
  double dual_tolerance = 0.0;

  // Omit an unattainable tile minimum while retaining its capacity and
  // contribution to every window density constraint.
  double minimumFillArea(size_t index) const
  {
    const double required = std::max(
        min_tile_density * tile_areas[index] - feature_areas[index], 0.0);
    return required <= max_fill_areas[index] ? required : 0.0;
  }

  double maximumFillArea(size_t index) const
  {
    const double budget
        = max_tile_density * tile_areas[index] - feature_areas[index];
    return budget < 0.0 ? max_fill_areas[index]
                        : std::min(max_fill_areas[index], budget);
  }

  double maximumWindowFillArea(double window_area, double feature_area) const
  {
    const double budget = max_window_density * window_area - feature_area;
    return budget < 0.0 ? std::numeric_limits<double>::infinity() : budget;
  }
};

struct FixedDissectionLpResult
{
  bool solved = false;
  double min_window_area = 0.0;
  double lip_value = 0.0;
  std::vector<double> fill_areas;
  std::string solver_status = "NOT_RUN";
  int solver_status_code = -1;
  // Backend iteration count; -1 means unavailable or solve not completed.
  int64_t solver_iterations = -1;
  double solve_seconds = 0.0;
  // Multiply exported LP area variables and row bounds by this value to
  // recover input area units. Public result areas already use input units.
  double model_area_scale = 1.0;
  bool model_exported = false;
  double diagnostics_seconds = 0.0;
};

}  // namespace fin

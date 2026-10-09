// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillLpSolver.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

#include "ortools/linear_solver/linear_solver.h"

namespace fin {

namespace {

void validateProblem(const FixedDissectionLpProblem& problem)
{
  const size_t tile_count = problem.tile_areas.size();
  if (problem.max_window_density < 0.0 || problem.max_window_density > 1.0) {
    throw std::invalid_argument(
        "max_window_density must be between zero and one.");
  }
  if (problem.min_tile_density < 0.0 || problem.min_tile_density > 1.0) {
    throw std::invalid_argument(
        "min_tile_density must be between zero and one.");
  }
  if (problem.max_tile_density < 0.0 || problem.max_tile_density > 1.0) {
    throw std::invalid_argument(
        "max_tile_density must be between zero and one.");
  }
  if (problem.min_window_density < 0.0 || problem.min_window_density > 1.0) {
    throw std::invalid_argument(
        "min_window_density must be between zero and one.");
  }
  if (tile_count == 0 || problem.feature_areas.size() != tile_count
      || problem.max_fill_areas.size() != tile_count) {
    throw std::invalid_argument(
        "Tile area, feature area, and fill capacity "
        "vectors must have the same nonzero size.");
  }
  if (problem.windows.empty()) {
    throw std::invalid_argument("The fixed dissection must contain a window.");
  }
  for (size_t index = 0; index < tile_count; index++) {
    if (problem.tile_areas[index] < 0.0 || problem.feature_areas[index] < 0.0
        || problem.max_fill_areas[index] < 0.0) {
      throw std::invalid_argument(
          "Tile areas and fill capacities must be nonnegative.");
    }
  }
  for (const auto& window : problem.windows) {
    if (window.empty()) {
      throw std::invalid_argument("A density window must contain a tile.");
    }
    for (const size_t tile_index : window) {
      if (tile_index >= tile_count) {
        throw std::invalid_argument(
            "A density window references an invalid tile index.");
      }
    }
  }
}

}  // namespace

FixedDissectionLpResult solveFillLp(const FixedDissectionLpProblem& problem,
                                    const FillLpObjective objective_type)
{
  validateProblem(problem);

  using operations_research::MPConstraint;
  using operations_research::MPSolver;
  using operations_research::MPVariable;

  std::unique_ptr<MPSolver> solver(MPSolver::CreateSolver("GLOP"));
  if (solver == nullptr) {
    return {};
  }

  const double infinity = solver->infinity();
  std::vector<MPVariable*> fill_variables;
  fill_variables.reserve(problem.tile_areas.size());
  for (size_t index = 0; index < problem.tile_areas.size(); index++) {
    const double min_fill_area
        = std::max(problem.min_tile_density * problem.tile_areas[index]
                       - problem.feature_areas[index],
                   0.0);
    const double max_tile_fill_area
        = problem.max_tile_density * problem.tile_areas[index]
          - problem.feature_areas[index];
    const double max_fill_area
        = std::min(problem.max_fill_areas[index], max_tile_fill_area);
    if (min_fill_area > max_fill_area) {
      return {};
    }
    fill_variables.push_back(solver->MakeNumVar(
        min_fill_area, max_fill_area, "p_" + std::to_string(index)));
  }

  MPVariable* min_window_area = nullptr;
  if (objective_type == FillLpObjective::MaximizeMinimumWindowArea) {
    min_window_area = solver->MakeNumVar(0.0, infinity, "M");
  }
  for (size_t index = 0; index < problem.windows.size(); index++) {
    const auto& window = problem.windows[index];
    double window_area = 0.0;
    double feature_area = 0.0;
    for (const size_t tile_index : window) {
      window_area += problem.tile_areas[tile_index];
      feature_area += problem.feature_areas[tile_index];
    }
    const double fill_budget = std::max(
        problem.max_window_density * window_area - feature_area, 0.0);
    MPConstraint* upper_bound = solver->MakeRowConstraint(
        -infinity, fill_budget, "upper_" + std::to_string(index));
    const double minimum_fill_area = std::max(
        problem.min_window_density * window_area - feature_area, 0.0);
    MPConstraint* lower_bound = solver->MakeRowConstraint(
        minimum_fill_area, infinity, "lower_" + std::to_string(index));
    MPConstraint* min_bound = nullptr;
    if (min_window_area != nullptr) {
      min_bound = solver->MakeRowConstraint(
          -infinity, feature_area, "minimum_" + std::to_string(index));
      min_bound->SetCoefficient(min_window_area, 1.0);
    }
    for (const size_t tile_index : window) {
      upper_bound->SetCoefficient(fill_variables[tile_index], 1.0);
      lower_bound->SetCoefficient(fill_variables[tile_index], 1.0);
      if (min_bound != nullptr) {
        min_bound->SetCoefficient(fill_variables[tile_index], -1.0);
      }
    }
  }

  operations_research::MPObjective* objective = solver->MutableObjective();
  if (min_window_area != nullptr) {
    objective->SetCoefficient(min_window_area, 1.0);
    objective->SetMaximization();
  } else {
    for (const MPVariable* fill : fill_variables) {
      objective->SetCoefficient(fill, 1.0);
    }
    objective->SetMinimization();
  }
  const MPSolver::ResultStatus status = solver->Solve();
  if (status != MPSolver::OPTIMAL && status != MPSolver::FEASIBLE) {
    return {};
  }

  FixedDissectionLpResult result;
  result.solved = true;
  result.fill_areas.reserve(fill_variables.size());
  for (const MPVariable* fill : fill_variables) {
    result.fill_areas.push_back(fill->solution_value());
  }
  result.min_window_area = infinity;
  for (const auto& window : problem.windows) {
    double window_area = 0.0;
    for (const size_t tile_index : window) {
      window_area
          += problem.feature_areas[tile_index] + result.fill_areas[tile_index];
    }
    result.min_window_area = std::min(result.min_window_area, window_area);
  }
  return result;
}

}  // namespace fin

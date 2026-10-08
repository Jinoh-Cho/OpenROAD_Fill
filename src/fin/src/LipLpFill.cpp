// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "LipLpFill.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

#include "ortools/linear_solver/linear_solver.h"

namespace fin {

std::vector<std::vector<size_t>> makeLipLpNeighborhoods(const TileGrid& grid,
                                                        int resolution,
                                                        LipLpType type)
{
  const auto& windows = grid.windows();
  const auto& tiles = grid.tiles();
  const size_t columns = grid.tileColumns();
  if (windows.empty() || tiles.empty() || columns == 0) {
    return {};
  }
  const size_t rows = (tiles.size() + columns - 1) / columns;
  const size_t tile_resolution = std::max(1, resolution);
  const auto window_tiles = grid.windowTileIndices();
  std::vector<std::vector<size_t>> tile_windows(tiles.size());
  for (size_t window = 0; window < window_tiles.size(); window++) {
    for (const size_t tile : window_tiles[window]) {
      tile_windows[tile].push_back(window);
    }
  }

  std::vector<std::vector<size_t>> groups;
  if (type == LipLpType::kLip1) {
    std::vector<std::vector<size_t>> rows_of_windows;
    for (size_t window = 0; window < windows.size(); window++) {
      const size_t row = windows[window].first_tile_y;
      if (rows_of_windows.size() <= row) {
        rows_of_windows.resize(row + 1);
      }
      rows_of_windows[row].push_back(window);
    }
    const size_t span = 2 * tile_resolution + 1;
    for (const auto& row : rows_of_windows) {
      for (size_t start = 0; start < row.size(); start++) {
        const size_t end = std::min(row.size(), start + span);
        groups.emplace_back(row.begin() + start, row.begin() + end);
      }
    }
  } else if (type == LipLpType::kLip2) {
    for (const auto& group : tile_windows) {
      if (!group.empty()) {
        groups.push_back(group);
      }
    }
  } else if (type == LipLpType::kLip3) {
    const size_t side = std::max<size_t>(1, tile_resolution / 2);
    for (size_t y = 0; y + side <= rows; y++) {
      for (size_t x = 0; x + side <= columns; x++) {
        std::vector<bool> included(windows.size(), true);
        for (size_t tile_y = y; tile_y < y + side; tile_y++) {
          for (size_t tile_x = x; tile_x < x + side; tile_x++) {
            const size_t tile = tile_y * columns + tile_x;
            std::vector<bool> covers_tile(windows.size(), false);
            for (const size_t window : tile_windows[tile]) {
              covers_tile[window] = true;
            }
            for (size_t window = 0; window < included.size(); window++) {
              included[window] = included[window] && covers_tile[window];
            }
          }
        }
        std::vector<size_t> group;
        for (size_t window = 0; window < included.size(); window++) {
          if (included[window]) {
            group.push_back(window);
          }
        }
        if (!group.empty()) {
          groups.push_back(std::move(group));
        }
      }
    }
  } else {
    throw std::invalid_argument("Lip LP type must be 1, 2, or 3.");
  }
  return groups;
}

FixedDissectionLpResult solveLipLpFill(
    const FixedDissectionLpProblem& problem,
    const std::vector<std::vector<size_t>>& neighborhoods)
{
  const size_t tile_count = problem.tile_areas.size();
  if (problem.max_window_density < 0.0 || problem.max_window_density > 1.0
      || problem.min_window_density < 0.0 || problem.min_window_density > 1.0
      || problem.min_tile_density < 0.0 || problem.min_tile_density > 1.0
      || problem.max_tile_density < 0.0 || problem.max_tile_density > 1.0) {
    throw std::invalid_argument(
        "LP density bounds must be between zero and one.");
  }
  if (tile_count == 0 || problem.feature_areas.size() != tile_count
      || problem.max_fill_areas.size() != tile_count || problem.windows.empty()
      || neighborhoods.empty()) {
    throw std::invalid_argument(
        "Lip LP input dimensions must be nonempty and consistent.");
  }
  for (const auto& window : problem.windows) {
    if (window.empty()) {
      throw std::invalid_argument("A density window must contain a tile.");
    }
    for (const size_t tile_index : window) {
      if (tile_index >= tile_count) {
        throw std::invalid_argument(
            "A density window references an invalid tile.");
      }
    }
  }
  for (const auto& group : neighborhoods) {
    if (group.empty()) {
      throw std::invalid_argument(
          "A Lipschitz neighborhood must contain a window.");
    }
    for (const size_t window_index : group) {
      if (window_index >= problem.windows.size()) {
        throw std::invalid_argument(
            "A Lipschitz neighborhood references an invalid window.");
      }
    }
  }

  using operations_research::MPConstraint;
  using operations_research::MPSolver;
  using operations_research::MPVariable;
  std::unique_ptr<MPSolver> solver(MPSolver::CreateSolver("GLOP"));
  if (solver == nullptr) {
    return {};
  }

  const double infinity = solver->infinity();
  std::vector<MPVariable*> fill_variables;
  fill_variables.reserve(tile_count);
  for (size_t index = 0; index < tile_count; index++) {
    const double min_fill
        = std::max(problem.min_tile_density * problem.tile_areas[index]
                       - problem.feature_areas[index],
                   0.0);
    const double max_fill
        = std::min(problem.max_fill_areas[index],
                   problem.max_tile_density * problem.tile_areas[index]
                       - problem.feature_areas[index]);
    if (problem.tile_areas[index] < 0.0 || problem.feature_areas[index] < 0.0
        || problem.max_fill_areas[index] < 0.0 || min_fill > max_fill) {
      return {};
    }
    fill_variables.push_back(
        solver->MakeNumVar(min_fill, max_fill, "p_" + std::to_string(index)));
  }

  std::vector<double> window_areas(problem.windows.size(), 0.0);
  std::vector<double> window_feature_areas(problem.windows.size(), 0.0);
  for (size_t index = 0; index < problem.windows.size(); index++) {
    for (const size_t tile_index : problem.windows[index]) {
      window_areas[index] += problem.tile_areas[tile_index];
      window_feature_areas[index] += problem.feature_areas[tile_index];
    }
    if (window_areas[index] <= 0.0) {
      throw std::invalid_argument("A density window must have positive area.");
    }
    const double min_fill
        = std::max(problem.min_window_density * window_areas[index]
                       - window_feature_areas[index],
                   0.0);
    const double max_fill
        = std::max(problem.max_window_density * window_areas[index]
                       - window_feature_areas[index],
                   0.0);
    MPConstraint* lower = solver->MakeRowConstraint(min_fill, infinity);
    MPConstraint* upper = solver->MakeRowConstraint(-infinity, max_fill);
    for (const size_t tile_index : problem.windows[index]) {
      lower->SetCoefficient(fill_variables[tile_index], 1.0);
      upper->SetCoefficient(fill_variables[tile_index], 1.0);
    }
  }

  MPVariable* lip = solver->MakeNumVar(0.0, 1.0, "lip");
  for (size_t group_index = 0; group_index < neighborhoods.size();
       group_index++) {
    MPVariable* min_density = solver->MakeNumVar(
        0.0, 1.0, "min_density_" + std::to_string(group_index));
    MPVariable* max_density = solver->MakeNumVar(
        0.0, 1.0, "max_density_" + std::to_string(group_index));
    MPConstraint* range = solver->MakeRowConstraint(0.0, infinity);
    range->SetCoefficient(lip, 1.0);
    range->SetCoefficient(max_density, -1.0);
    range->SetCoefficient(min_density, 1.0);
    for (const size_t window_index : neighborhoods[group_index]) {
      MPConstraint* maximum = solver->MakeRowConstraint(
          -infinity, -window_feature_areas[window_index]);
      maximum->SetCoefficient(max_density, -window_areas[window_index]);
      MPConstraint* minimum = solver->MakeRowConstraint(
          -window_feature_areas[window_index], infinity);
      minimum->SetCoefficient(min_density, -window_areas[window_index]);
      for (const size_t tile_index : problem.windows[window_index]) {
        maximum->SetCoefficient(fill_variables[tile_index], 1.0);
        minimum->SetCoefficient(fill_variables[tile_index], 1.0);
      }
    }
  }

  solver->MutableObjective()->SetCoefficient(lip, 1.0);
  solver->MutableObjective()->SetMinimization();
  const MPSolver::ResultStatus status = solver->Solve();
  if (status != MPSolver::OPTIMAL && status != MPSolver::FEASIBLE) {
    return {};
  }

  FixedDissectionLpResult result;
  result.solved = true;
  result.lip_value = lip->solution_value();
  result.fill_areas.reserve(fill_variables.size());
  for (const MPVariable* fill : fill_variables) {
    result.fill_areas.push_back(fill->solution_value());
  }
  result.min_window_area = infinity;
  for (const auto& window : problem.windows) {
    double area = 0.0;
    for (const size_t tile_index : window) {
      area += problem.feature_areas[tile_index] + result.fill_areas[tile_index];
    }
    result.min_window_area = std::min(result.min_window_area, area);
  }
  return result;
}

}  // namespace fin

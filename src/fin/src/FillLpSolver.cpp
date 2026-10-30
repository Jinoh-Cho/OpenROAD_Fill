// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillLpSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "LpDiagnostics.h"
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

void configureFillLpDualTolerance(const FixedDissectionLpProblem& problem,
                                  operations_research::MPSolver& solver)
{
  if (!std::isfinite(problem.dual_tolerance) || problem.dual_tolerance < 0.0) {
    throw std::invalid_argument(
        "Dual tolerance must be finite and nonnegative.");
  }
  if (problem.dual_tolerance == 0.0) {
    return;
  }
  if (problem.lp_solver != FillLpSolver::Pdlp) {
    throw std::invalid_argument("Dual tolerance override requires PDLP.");
  }
  std::ostringstream parameters;
  parameters << std::scientific << std::setprecision(17)
             << "termination_criteria { detailed_optimality_criteria { "
             << "eps_optimal_primal_residual_absolute: 1e-6 "
             << "eps_optimal_primal_residual_relative: 1e-6 "
             << "eps_optimal_dual_residual_absolute: " << problem.dual_tolerance
             << " eps_optimal_dual_residual_relative: "
             << problem.dual_tolerance
             << " eps_optimal_objective_gap_absolute: 1e-6 "
             << "eps_optimal_objective_gap_relative: 1e-6 } }";
  if (!solver.SetSolverSpecificParametersAsString(parameters.str())) {
    throw std::runtime_error("Failed to configure PDLP dual tolerance.");
  }
}

FixedDissectionLpResult solveFillLp(const FixedDissectionLpProblem& problem,
                                    const FillLpObjective objective_type)
{
  validateProblem(problem);

  using operations_research::MPConstraint;
  using operations_research::MPSolver;
  using operations_research::MPVariable;

  if (problem.lp_threads < 1) {
    throw std::invalid_argument("LP thread count must be positive.");
  }
  if (problem.lp_solver != FillLpSolver::Pdlp && problem.lp_threads != 1) {
    throw std::invalid_argument("Multiple LP threads require the PDLP solver.");
  }
  const char* solver_name = fillLpSolverName(problem.lp_solver);
  std::unique_ptr<MPSolver> solver(MPSolver::CreateSolver(solver_name));
  if (solver == nullptr) {
    throw std::runtime_error(std::string("LP solver is unavailable: ")
                             + solver_name);
  }

  configureFillLpDualTolerance(problem, *solver);

  if (problem.solver_log) {
    solver->EnableOutput();
  }

  if (problem.lp_solver == FillLpSolver::Pdlp) {
    const auto thread_status = solver->SetNumThreads(problem.lp_threads);
    if (!thread_status.ok()) {
      throw std::runtime_error("Failed to configure PDLP threads: "
                               + thread_status.ToString());
    }
  }

  // Use one common area unit so clipped edge tiles keep their relative sizes
  // and all constraint coefficients remain unchanged. A full tile then has
  // area one, regardless of the design's DBU scale.
  const double largest_tile_area
      = *std::max_element(problem.tile_areas.begin(), problem.tile_areas.end());
  const double area_scale = largest_tile_area > 0.0 ? largest_tile_area : 1.0;
  const double infinity = solver->infinity();
  std::vector<MPVariable*> fill_variables;
  fill_variables.reserve(problem.tile_areas.size());
  for (size_t index = 0; index < problem.tile_areas.size(); index++) {
    const double min_fill_area = problem.minimumFillArea(index);
    const double max_fill_area = problem.maximumFillArea(index);
    if (min_fill_area > max_fill_area) {
      return {};
    }
    fill_variables.push_back(solver->MakeNumVar(min_fill_area / area_scale,
                                                max_fill_area / area_scale,
                                                "p_" + std::to_string(index)));
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
    const double fill_budget
        = problem.maximumWindowFillArea(window_area, feature_area);
    MPConstraint* upper_bound = solver->MakeRowConstraint(
        -infinity, fill_budget / area_scale, "upper_" + std::to_string(index));
    const double minimum_fill_area = std::max(
        problem.min_window_density * window_area - feature_area, 0.0);
    MPConstraint* lower_bound
        = solver->MakeRowConstraint(minimum_fill_area / area_scale,
                                    infinity,
                                    "lower_" + std::to_string(index));
    MPConstraint* min_bound = nullptr;
    if (min_window_area != nullptr) {
      min_bound = solver->MakeRowConstraint(-infinity,
                                            feature_area / area_scale,
                                            "minimum_" + std::to_string(index));
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
  const char* method
      = objective_type == FillLpObjective::MaximizeMinimumWindowArea
            ? "lp_minvar"
            : "min_amount";
  const auto start_time
      = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::cout << "[Fill LP] " << solver_name << " solve start method=" << method
            << " time="
            << std::put_time(std::localtime(&start_time),
                             "%Y-%m-%d %H:%M:%S %Z")
            << " solver_log=" << problem.solver_log
            << " lp_threads=" << problem.lp_threads
            << " area_scale=" << area_scale
            << " variables=" << solver->NumVariables()
            << " constraints=" << solver->NumConstraints() << std::endl;
  const auto solve_start = std::chrono::steady_clock::now();
  const MPSolver::ResultStatus status = solver->Solve();
  const double solve_seconds
      = std::chrono::duration<double>(std::chrono::steady_clock::now()
                                      - solve_start)
            .count();
  const int64_t solver_iterations
      = status == MPSolver::OPTIMAL || status == MPSolver::FEASIBLE
            ? solver->iterations()
            : -1;
  const auto end_time
      = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::cout << "[Fill LP] " << solver_name << " solve end method=" << method
            << " time="
            << std::put_time(std::localtime(&end_time), "%Y-%m-%d %H:%M:%S %Z")
            << " solve_seconds=" << solve_seconds
            << " status=" << static_cast<int>(status)
            << " iterations=" << solver_iterations << std::endl;
  if (status != MPSolver::OPTIMAL && status != MPSolver::FEASIBLE) {
    return recordFillLpFailure(
        problem, *solver, status, solve_seconds, area_scale);
  }

  FixedDissectionLpResult result;
  result.solved = true;
  result.solver_status = fillLpStatusName(status);
  result.solver_status_code = status;
  result.solver_iterations = solver_iterations;
  result.solve_seconds = solve_seconds;
  result.model_area_scale = area_scale;
  result.fill_areas.reserve(fill_variables.size());
  for (const MPVariable* fill : fill_variables) {
    result.fill_areas.push_back(fill->solution_value() * area_scale);
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

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "LPFill.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "FillCommon.h"
#include "odb/db.h"
#include "ortools/linear_solver/linear_solver.h"

namespace fin {
namespace {
using operations_research::MPConstraint;
using operations_research::MPSolver;
using operations_research::MPVariable;
using Clock = std::chrono::steady_clock;

const char* solverStatus(const MPSolver::ResultStatus status)
{
  switch (status) {
    case MPSolver::OPTIMAL:
      return "optimal";
    case MPSolver::FEASIBLE:
      return "feasible";
    case MPSolver::INFEASIBLE:
      return "infeasible";
    case MPSolver::UNBOUNDED:
      return "unbounded";
    case MPSolver::ABNORMAL:
      return "abnormal";
    case MPSolver::MODEL_INVALID:
      return "invalid model";
    case MPSolver::NOT_SOLVED:
      return "not solved";
  }
  return "unknown";
}

void validateProblem(const FillLPProblem& problem)
{
  const size_t count = problem.tile_areas.size();
  if (count == 0 || problem.metal_areas.size() != count
      || problem.capacities.size() != count || problem.windows.empty()) {
    throw std::invalid_argument(
        "LP input must have matching tile arrays and nonempty windows.");
  }
  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(problem.tile_areas[i]) || problem.tile_areas[i] <= 0
        || !std::isfinite(problem.metal_areas[i]) || problem.metal_areas[i] < 0
        || !std::isfinite(problem.capacities[i]) || problem.capacities[i] < 0) {
      throw std::invalid_argument(
          "LP areas must be finite and nonnegative, with positive tile areas.");
    }
  }
  for (const auto& window : problem.windows) {
    if (window.tiles.empty()) {
      throw std::invalid_argument("LP windows must contain at least one tile.");
    }
    auto indices = window.tiles;
    std::ranges::sort(indices);
    if (indices.back() >= count
        || std::adjacent_find(indices.begin(), indices.end())
               != indices.end()) {
      throw std::invalid_argument(
          "LP windows must reference distinct valid tiles.");
    }
  }
}

MPVariable* makeTileFillVariable(MPSolver& solver,
                                 const FillLPProblem& problem,
                                 const size_t tile,
                                 const LPFillOptions& options,
                                 const double scale,
                                 FillLPSolution& solution)
{
  const double requested
      = std::max(options.min_tile_density * problem.tile_areas[tile]
                     - problem.metal_areas[tile],
                 0.0);
  const double lower = std::min(requested, problem.capacities[tile]);
  double upper = problem.capacities[tile];
  if (options.max_tile_density < 1.0) {
    upper
        = std::min(upper,
                   std::max(options.max_tile_density * problem.tile_areas[tile]
                                - problem.metal_areas[tile],
                            0.0));
  }
  solution.relaxed_tiles += lower < requested;
  return solver.MakeNumVar(
      lower / scale, upper / scale, "fill_" + std::to_string(tile));
}

void addWindowConstraints(MPSolver& solver,
                          const FillLPProblem& problem,
                          const size_t window_index,
                          const LPFillOptions& options,
                          const std::vector<MPVariable*>& fills,
                          MPVariable* minimum_density,
                          const double scale,
                          FillLPSolution& solution)
{
  const auto& window = problem.windows[window_index];
  double area = 0.0;
  double metal = 0.0;
  double capacity = 0.0;
  for (const size_t tile : window.tiles) {
    area += problem.tile_areas[tile];
    metal += problem.metal_areas[tile];
    capacity += problem.capacities[tile];
  }
  const double requested = std::max(options.min_density * area - metal, 0.0);
  const double lower = std::min(requested, capacity);
  const double upper = metal > options.max_density * area
                           ? solver.infinity()
                           : (options.max_density * area - metal) / scale;
  MPConstraint* bounds = solver.MakeRowConstraint(
      lower / scale, upper, "window_" + std::to_string(window_index));
  MPConstraint* objective_bound = nullptr;
  if (minimum_density != nullptr) {
    objective_bound
        = solver.MakeRowConstraint(-metal / scale,
                                   solver.infinity(),
                                   "minimum_" + std::to_string(window_index));
    objective_bound->SetCoefficient(minimum_density, -area / scale);
  }
  for (const size_t tile : window.tiles) {
    bounds->SetCoefficient(fills[tile], 1.0);
    if (objective_bound != nullptr) {
      objective_bound->SetCoefficient(fills[tile], 1.0);
    }
  }
  if (lower < requested) {
    ++solution.relaxed_windows;
    solution.relaxed_minimums.push_back(
        {window_index, (metal + lower) / area, requested, capacity});
  }
}

int toDbu(const double microns, const int dbu)
{
  const double value = std::round(microns * dbu);
  if (!std::isfinite(value) || value < 1
      || value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(
        "Window size and step must be positive and representable in DBU.");
  }
  return static_cast<int>(value);
}

struct LayerCandidates
{
  FillLPProblem problem;
  std::vector<std::vector<Rectangle>> candidates;
};

LayerCandidates prepareLayer(odb::dbBlock* block,
                             odb::dbTechLayer* layer,
                             const FillGrid& grid,
                             const FillShapesConfig& config,
                             const int num_threads)
{
  Polygon90Set metal = collectNonFillShapes(block, layer, false);
  for (auto* fill : block->getFills()) {
    if (fill->getTechLayer() == layer) {
      odb::Rect rect;
      fill->getRect(rect);
      metal.insert(
          makeRect(rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax()));
    }
  }
  for (auto* term : block->getBTerms()) {
    for (auto* pin : term->getBPins()) {
      for (auto* box : pin->getBoxes()) {
        if (box->getTechLayer() == layer) {
          metal.insert(
              makeRect(box->xMin(), box->yMin(), box->xMax(), box->yMax()));
        }
      }
    }
  }
  std::vector<Rectangle> rectangles;
  metal.get_rectangles(rectangles);
  LayerCandidates result;
  result.problem.metal_areas = grid.metalAreas(rectangles, num_threads);
  Polygon90Set blocked = metal;
  for (auto* obstruction : block->getObstructions()) {
    const auto* box = obstruction->getBBox();
    if (obstruction->isFillObstruction() && box->getTechLayer() == layer) {
      blocked.insert(
          makeRect(box->xMin(), box->yMin(), box->xMax(), box->yMax()));
    }
  }
  blocked = blocked + config.space_to_non_fill;
  rectangles.clear();
  blocked.get_rectangles(rectangles);
  auto obstacles = grid.partition(rectangles);
  const size_t count = grid.tiles().size();
  result.candidates.resize(count);
  result.problem.capacities.resize(count);
  result.problem.tile_areas.resize(count);
  result.problem.windows = grid.windows();
  const auto [space_x, space_y] = getSpacing(layer, config);
  forEachFillTile(count, num_threads, [&](const size_t i) {
    const auto& tile = grid.tiles()[i];
    Polygon90Set area;
    area.insert(makeRect(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax()));
    shrink(area, space_x, space_x, space_y, space_y);
    area -= obstacles[i];
    pruneFillArea(area, layer, config);
    result.candidates[i] = makeFillCandidates(area, layer, config);
    for (const auto& candidate : result.candidates[i]) {
      result.problem.capacities[i] += rectangleArea(candidate);
    }
    result.problem.tile_areas[i] = static_cast<double>(tile.area());
  });
  return result;
}

std::vector<std::vector<Rectangle>> selectLayer(const LayerCandidates& layer,
                                                const FillLPSolution& solution,
                                                const LPFillOptions& options)
{
  std::vector<std::vector<Rectangle>> selected(layer.candidates.size());
  forEachFillTile(selected.size(), options.num_threads, [&](const size_t i) {
    selected[i]
        = selectFillCandidates(layer.candidates[i], solution.fill_areas[i]);
  });
  return selected;
}
struct PlacedLayer
{
  std::vector<double> metal_areas;
  double area = 0.0;
  size_t fill_count = 0;
};

PlacedLayer insertLayerFills(
    odb::dbBlock* block,
    odb::dbTechLayer* layer,
    const std::vector<std::vector<Rectangle>>& selected,
    const std::vector<double>& metal_areas,
    const int num_masks,
    std::vector<odb::dbFill*>& created)
{
  PlacedLayer result{metal_areas};
  const int masks = std::max(num_masks, 1);
  for (size_t i = 0; i < selected.size(); ++i) {
    for (const auto& rectangle : selected[i]) {
      const int mask = masks == 1 ? 0 : result.fill_count % masks + 1;
      created.push_back(odb::dbFill::create(block,
                                            false,
                                            mask,
                                            layer,
                                            xl(rectangle),
                                            yl(rectangle),
                                            xh(rectangle),
                                            yh(rectangle)));
      const double area = rectangleArea(rectangle);
      result.metal_areas[i] += area;
      result.area += area;
      ++result.fill_count;
    }
  }
  return result;
}

struct LayerFillStatistics
{
  FillDensitySummary density;
  double area_um2;
  size_t fill_count;
  double prepare_seconds;
  double solve_seconds;
  double placement_seconds;
};

void reportWindowDensity(utl::Logger* logger,
                         odb::dbTechLayer* layer,
                         const size_t count,
                         const double minimum,
                         const double maximum,
                         const FillDensitySummary& before,
                         const FillDensitySummary& after)
{
  logger->info(utl::FIN,
               22,
               "LP density: layer={}, scope=window, count={}, "
               "requested=[{:.4f}%, {:.4f}%].",
               layer->getConstName(),
               count,
               minimum * 100,
               maximum * 100);
  const auto report
      = [&](const char* phase, const FillDensitySummary& summary) {
          logger->info(utl::FIN,
                       23,
                       "  {}: min={:.4f}%, max={:.4f}%, mean={:.4f}%, "
                       "range={:.4f} pp, below_minimum={}, above_maximum={}.",
                       phase,
                       summary.minimum * 100,
                       summary.maximum * 100,
                       summary.mean * 100,
                       (summary.maximum - summary.minimum) * 100,
                       summary.below_minimum,
                       summary.above_maximum);
        };
  report("before fill", before);
  report("after fill", after);
}

void reportLayerFill(utl::Logger* logger,
                     odb::dbTechLayer* layer,
                     const LPFillOptions& options,
                     const FillLPSolution& solution,
                     const size_t window_count,
                     const LayerFillStatistics& statistics)
{
  if (solution.relaxed_tiles != 0) {
    logger->warn(utl::FIN,
                 19,
                 "Layer {}: {} tile lower bounds are limited by candidate "
                 "capacity; requested minimum tile density is {:.6f}.",
                 layer->getConstName(),
                 solution.relaxed_tiles,
                 options.min_tile_density);
  }
  if (solution.relaxed_windows != 0) {
    logger->warn(utl::FIN,
                 15,
                 "Layer {}: {} of {} window lower bounds are limited by "
                 "candidate capacity; "
                 "requested minimum density is {:.6f}.",
                 layer->getConstName(),
                 solution.relaxed_windows,
                 window_count,
                 options.min_density);
  }
  for (const auto& window : solution.relaxed_minimums) {
    debugPrint(logger,
               utl::FIN,
               "lp_fill",
               1,
               "Layer {}, window {}: requested_minimum={:.6f}, "
               "effective_minimum={:.6f}, "
               "required_fill={:.6f} DBU^2, available_fill={:.6f} DBU^2.",
               layer->getConstName(),
               window.window_index,
               options.min_density,
               window.effective_density,
               window.requested_fill_area,
               window.available_fill_area);
  }
  logger->info(utl::FIN,
               16,
               "LP fill: layer={}, objective={}, solver={}, iterations={}, "
               "fills={}, area={:.6f} um^2, "
               "density_min={:.6f}, density_max={:.6f}, below_minimum={}, "
               "above_maximum={}, "
               "prepare={:.3f}s, solve={:.3f}s, placement={:.3f}s.",
               layer->getConstName(),
               options.objective,
               options.solver,
               solution.iterations,
               statistics.fill_count,
               statistics.area_um2,
               statistics.density.minimum,
               statistics.density.maximum,
               statistics.density.below_minimum,
               statistics.density.above_maximum,
               statistics.prepare_seconds,
               statistics.solve_seconds,
               statistics.placement_seconds);
}

}

void validateLPFillOptions(const LPFillOptions& options)
{
  if (options.rules_file.empty()) {
    throw std::invalid_argument("A fill rules file is required.");
  }
  if (options.objective != "min_fill" && options.objective != "min_var") {
    throw std::invalid_argument("Objective must be min_fill or min_var.");
  }
  if (options.solver != "glop" && options.solver != "pdlp") {
    throw std::invalid_argument("Solver must be glop or pdlp.");
  }
  if (options.solver_threads < 1 || options.num_threads < 1) {
    throw std::invalid_argument("Thread counts must be positive.");
  }
  for (const double value : {options.window_width, options.window_height}) {
    if (!std::isfinite(value) || value <= 0) {
      throw std::invalid_argument(
          "Window dimensions must be finite and positive.");
    }
  }
  for (const double ratio : {options.step_x, options.step_y}) {
    if (!std::isfinite(ratio) || ratio <= 0 || ratio > 1) {
      throw std::invalid_argument(
          "Window step ratios must satisfy 0 < ratio <= 1.");
    }
  }
  if (!std::isfinite(options.min_density) || !std::isfinite(options.max_density)
      || options.min_density < 0 || options.max_density > 1
      || options.min_density > options.max_density) {
    throw std::invalid_argument(
        "Density bounds must satisfy 0 <= min_density <= max_density <= 1.");
  }
  if (!std::isfinite(options.min_tile_density)
      || !std::isfinite(options.max_tile_density)
      || options.min_tile_density < 0 || options.max_tile_density > 1
      || options.min_tile_density > options.max_tile_density) {
    throw std::invalid_argument(
        "Tile density bounds must satisfy 0 <= min_tile_density <= "
        "max_tile_density <= 1.");
  }
}

FillLPSolution solveFillLP(const FillLPProblem& problem,
                           const LPFillOptions& options)
{
  validateLPFillOptions(options);
  validateProblem(problem);
  std::unique_ptr<MPSolver> solver(
      MPSolver::CreateSolver(options.solver == "glop" ? "GLOP" : "PDLP"));
  if (solver == nullptr) {
    throw std::runtime_error("Requested LP solver is unavailable.");
  }
  if (options.solver == "pdlp") {
    const auto status = solver->SetNumThreads(options.solver_threads);
    if (!status.ok()) {
      throw std::runtime_error("Cannot configure PDLP threads: "
                               + status.ToString());
    }
  }
  const double scale = *std::ranges::max_element(problem.tile_areas);
  FillLPSolution solution;
  std::vector<MPVariable*> fills;
  for (size_t i = 0; i < problem.tile_areas.size(); ++i) {
    fills.push_back(
        makeTileFillVariable(*solver, problem, i, options, scale, solution));
  }
  MPVariable* minimum = options.objective == "min_var"
                            ? solver->MakeNumVar(0.0, 1.0, "minimum_density")
                            : nullptr;
  for (size_t i = 0; i < problem.windows.size(); ++i) {
    addWindowConstraints(
        *solver, problem, i, options, fills, minimum, scale, solution);
  }
  auto* objective = solver->MutableObjective();
  if (minimum != nullptr) {
    objective->SetCoefficient(minimum, 1.0);
    objective->SetMaximization();
  } else {
    for (const auto* fill : fills) {
      objective->SetCoefficient(fill, 1.0);
    }
    objective->SetMinimization();
  }
  const auto status = solver->Solve();
  solution.status = solverStatus(status);
  solution.iterations = solver->iterations();
  solution.solved = status == MPSolver::OPTIMAL;
  if (solution.solved) {
    for (size_t i = 0; i < fills.size(); ++i) {
      solution.fill_areas.push_back(std::clamp(
          fills[i]->solution_value() * scale, 0.0, problem.capacities[i]));
    }
  }
  return solution;
}

LPFill::LPFill(odb::dbDatabase* db, utl::Logger* logger)
    : db_(db), logger_(logger)
{
}

void LPFill::fill(const LPFillOptions& options)
{
  validateLPFillOptions(options);
  if (db_->getTech() == nullptr || db_->getChip() == nullptr
      || db_->getChip()->getBlock() == nullptr) {
    logger_->error(utl::FIN, 11, "lp_fill requires a loaded design.");
  }
  auto* block = db_->getChip()->getBlock();
  const int dbu = db_->getTech()->getDbUnitsPerMicron();
  const FillGrid grid(
      block->getCoreArea(),
      odb::Point(toDbu(options.window_width, dbu),
                 toDbu(options.window_height, dbu)),
      odb::Point(toDbu(options.window_width * options.step_x, dbu),
                 toDbu(options.window_height * options.step_y, dbu)));
  const auto layers
      = loadFillRules(options.rules_file.c_str(), db_->getTech(), logger_);
  if (options.solver == "glop" && options.solver_threads != 1) {
    logger_->warn(utl::FIN,
                  12,
                  "GLOP is single-threaded; ignoring -solver_threads {}.",
                  options.solver_threads);
  }
  std::vector<odb::dbFill*> created;
  try {
    for (auto* layer : db_->getTech()->getLayers()) {
      const auto it = layers.find(layer);
      if (it != layers.end()) {
        auto config = it->second;
        config.non_opc.pattern_origin = block->getCoreArea().ll();
        fillLayer(block, layer, grid, config, options, created);
      }
    }
  } catch (...) {
    for (auto* fill : created) {
      odb::dbFill::destroy(fill);
    }
    throw;
  }
}

void LPFill::fillLayer(odb::dbBlock* block,
                       odb::dbTechLayer* layer,
                       const FillGrid& grid,
                       const FillLayerConfig& config,
                       const LPFillOptions& options,
                       std::vector<odb::dbFill*>& created)
{
  if (config.has_opc) {
    logger_->error(
        utl::FIN,
        13,
        "lp_fill supports non-OPC rules only; layer {} has OPC rules.",
        layer->getConstName());
  }
  const auto start = Clock::now();
  auto candidates
      = prepareLayer(block, layer, grid, config.non_opc, options.num_threads);
  const auto prepared = Clock::now();
  const auto solution = solveFillLP(candidates.problem, options);
  const auto solved = Clock::now();
  if (!solution.solved) {
    logger_->error(utl::FIN,
                   14,
                   "LP fill failed on layer {}: {}. No fill from this command "
                   "is retained.",
                   layer->getConstName(),
                   solution.status);
  }
  const auto selected = selectLayer(candidates, solution, options);
  const auto placed = insertLayerFills(block,
                                       layer,
                                       selected,
                                       candidates.problem.metal_areas,
                                       config.num_masks,
                                       created);
  const auto summary
      = summarizeFillDensity(grid.windowDensities(placed.metal_areas),
                             options.min_density,
                             options.max_density);
  const auto before_summary = summarizeFillDensity(
      grid.windowDensities(candidates.problem.metal_areas),
      options.min_density,
      options.max_density);
  reportWindowDensity(logger_,
                      layer,
                      grid.windows().size(),
                      options.min_density,
                      options.max_density,
                      before_summary,
                      summary);
  const auto placed_time = Clock::now();
  const auto seconds = [](const auto from, const auto to) {
    return std::chrono::duration<double>(to - from).count();
  };
  const double dbu = db_->getTech()->getDbUnitsPerMicron();
  reportLayerFill(logger_,
                  layer,
                  options,
                  solution,
                  grid.windows().size(),
                  {summary,
                   placed.area / (dbu * dbu),
                   placed.fill_count,
                   seconds(start, prepared),
                   seconds(prepared, solved),
                   seconds(solved, placed_time)});
}
}

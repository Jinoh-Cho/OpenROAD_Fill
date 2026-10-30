// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "LPFill.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "DensityAnalyzer.h"
#include "FillConfig.h"
#include "FillGeometry.h"
#include "FillReporter.h"
#include "FillSvgWriter.h"
#include "FixedDissectionLp.h"
#include "LPFillUtil.h"
#include "LipLpFill.h"
#include "LpDiagnostics.h"
#include "MinFillAmountLp.h"
#include "MinVarLP.h"
#include "graphics.h"
#include "polygon.h"

namespace fin {

using utl::FIN;

using odb::dbBlock;
using odb::dbDatabase;
using odb::dbFill;
using odb::dbTechLayer;
using odb::Rect;

LPFill::LPFill(dbDatabase* db, utl::Logger* logger, bool debug)
    : db_(db), logger_(logger)
{
  if (debug && Graphics::guiActive()) {
    graphics_ = std::make_unique<Graphics>();
  }
}

LPFill::~LPFill() = default;

double LPFill::tileGridMetalArea(const char* rules_filename,
                                 const Rect& region,
                                 const odb::Point& origin,
                                 int window_size,
                                 int resolution,
                                 double min_window_density,
                                 double max_density,
                                 const char* svg_filename,
                                 const char* density_report_filename,
                                 const char* floating_density_profile_filename,
                                 const char* floating_density_algorithm)
{
  using Clock = std::chrono::steady_clock;
  const auto seconds
      = [](const Clock::time_point& start, const Clock::time_point& end) {
          return std::chrono::duration<double>(end - start).count();
        };
  auto compute_slice_start = Clock::now();
  double compute_seconds = 0.0;
  double svg_seconds = 0.0;
  const std::string algorithm = floating_density_algorithm == nullptr
                                    ? "alg3"
                                    : floating_density_algorithm;
  if (algorithm != "alg2" && algorithm != "alg3") {
    logger_->error(FIN,
                   67,
                   "Unknown floating density algorithm '{}'; expected alg2 "
                   "or alg3.",
                   algorithm);
  }
  std::vector<TileDensityReport> density_reports;
  const auto layers
      = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  TileGrid grid({region, origin, window_size, resolution});
  double total_area = 0.0;
  for (auto* layer : db_->getTech()->getLayers()) {
    if (layers.find(layer) == layers.end()) {
      continue;
    }
    const auto layer_start = Clock::now();
    const Polygon90Set wire_shapes = orNonFills(block, layer);
    Polygon90Set fill_shapes;
    for (dbFill* fill : block->getFills()) {
      if (fill->getTechLayer() != layer) {
        continue;
      }
      Rect fill_rect;
      fill->getRect(fill_rect);
      fill_shapes += makeRect(fill_rect.xMin(),
                              fill_rect.yMin(),
                              fill_rect.xMax(),
                              fill_rect.yMax());
    }
    Polygon90Set layer_shapes = wire_shapes;
    layer_shapes += fill_shapes;
    grid.calculateMetalDensities(layer_shapes);
    const double layer_area = grid.totalMetalArea();
    const WindowDensitySummary density_summary = summarizeWindowDensities(
        grid.windows(), min_window_density, max_density);
    const double layer_compute_seconds = seconds(layer_start, Clock::now());
    logger_->info(
        FIN,
        17,
        "Tile-grid density: layer={}, tiles={}, windows={}, metal_area={:.0f} "
        "DBU^2, min={:.6f}, max={:.6f}, mean={:.6f}, variance={:.8f}, "
        "min_window_density_limit={:.6f}, min_violation_windows={}, "
        "max_density_limit={:.6f}, max_violation_windows={}, "
        "compute={:.3f}s (SVG excluded).",
        layer->getConstName(),
        grid.tiles().size(),
        grid.windows().size(),
        layer_area,
        density_summary.min_density,
        density_summary.max_density,
        density_summary.mean_density,
        density_summary.variance,
        min_window_density,
        density_summary.min_violation_count,
        max_density,
        density_summary.max_violation_count,
        layer_compute_seconds);
    density_reports.push_back({layer->getConstName(),
                               grid.windows().size(),
                               density_summary.min_density,
                               density_summary.max_density,
                               density_summary.mean_density,
                               density_summary.variance,
                               density_summary.min_violation_count,
                               density_summary.max_violation_count,
                               makeWindowDensityHistogram(grid.windows()),
                               layer_compute_seconds,
                               0.0});
    if (floating_density_profile_filename != nullptr
        && floating_density_profile_filename[0] != '\0') {
      std::vector<Rectangle> metal_rectangles;
      get_rectangles(metal_rectangles, layer_shapes);
      std::vector<Rect> rectangles;
      rectangles.reserve(metal_rectangles.size());
      for (const Rectangle& rectangle : metal_rectangles) {
        rectangles.emplace_back(
            xl(rectangle), yl(rectangle), xh(rectangle), yh(rectangle));
      }
      const FloatingDensityResult floating_density
          = algorithm == "alg2"
                ? analyzeFloatingDensityAlg2(rectangles, region, window_size)
                : analyzeFloatingDensityAlg3(rectangles, region, window_size);
      const std::string profile_filename
          = std::string(floating_density_profile_filename) + "_"
            + layer->getConstName() + ".json";
      writeFloatingDensityProfile(profile_filename,
                                  layer->getConstName(),
                                  algorithm,
                                  floating_density,
                                  min_window_density,
                                  max_density,
                                  logger_);
    }
    total_area += layer_area;
    if (svg_filename != nullptr && svg_filename[0] != '\0') {
      compute_seconds += seconds(compute_slice_start, Clock::now());
      const auto svg_start = Clock::now();
      writeTileGridSvg(
          grid,
          std::string(svg_filename) + "_" + layer->getConstName() + ".svg",
          wire_shapes,
          db_->getTech()->getDbUnitsPerMicron(),
          nullptr,
          true,
          &fill_shapes);
      const double layer_svg_seconds = seconds(svg_start, Clock::now());
      density_reports.back().svg_seconds = layer_svg_seconds;
      svg_seconds += layer_svg_seconds;
      compute_slice_start = Clock::now();
    }
  }
  compute_seconds += seconds(compute_slice_start, Clock::now());
  logger_->info(FIN,
                58,
                "Tile-grid density total runtime (SVG excluded): {:.3f}s; "
                "SVG rendering: {:.3f}s.",
                compute_seconds,
                svg_seconds);
  writeTileDensityReport(density_report_filename,
                         density_reports,
                         compute_seconds,
                         svg_seconds,
                         min_window_density,
                         max_density,
                         logger_);
  return total_area;
}

double LPFill::fillLp(const char* rules_filename,
                      const Rect& region,
                      const odb::Point& origin,
                      int window_size,
                      int resolution,
                      double min_tile_density,
                      double max_tile_density,
                      double min_window_density,
                      double max_window_density,
                      const char* svg_filename,
                      const char* density_report_filename,
                      const FillMethod method,
                      const char* lp_solver,
                      int lp_threads,
                      const char* lp_diagnostics,
                      int candidate_threads,
                      int placement_threads)
{
  FillLpSolver selected_solver;
  if (lp_solver != nullptr && std::string(lp_solver) == "glop") {
    selected_solver = FillLpSolver::Glop;
  } else if (lp_solver != nullptr && std::string(lp_solver) == "pdlp") {
    selected_solver = FillLpSolver::Pdlp;
  } else {
    throw std::invalid_argument("LP solver must be glop or pdlp.");
  }
  if (lp_threads < 1) {
    throw std::invalid_argument("LP thread count must be positive.");
  }
  if (selected_solver != FillLpSolver::Pdlp && lp_threads != 1) {
    throw std::invalid_argument("Multiple LP threads require the PDLP solver.");
  }
  if (candidate_threads < 1) {
    throw std::invalid_argument("Candidate thread count must be positive.");
  }
  if (placement_threads < 1) {
    throw std::invalid_argument("Placement thread count must be positive.");
  }
  const bool minimize_total_fill_area = method == FillMethod::MinFillAmount;
  int lip_type = 0;
  switch (method) {
    case FillMethod::MinVar:
    case FillMethod::MinFillAmount:
      break;
    case FillMethod::Lip1:
      lip_type = 1;
      break;
    case FillMethod::Lip2:
      lip_type = 2;
      break;
    case FillMethod::Lip3:
      lip_type = 3;
      break;
    default:
      throw std::invalid_argument("Unknown LP fill method.");
  }

  using Clock = std::chrono::steady_clock;
  const auto seconds
      = [](const Clock::time_point& start, const Clock::time_point& end) {
          return std::chrono::duration<double>(end - start).count();
        };
  const auto compute_start = Clock::now();
  auto compute_slice_start = compute_start;
  double compute_seconds = 0.0;
  double svg_seconds = 0.0;
  std::vector<LPFillDensityReport> density_reports;
  const auto layers
      = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  TileGrid grid({region, origin, window_size, resolution});
  double total_placed_area = 0.0;
  std::vector<dbFill*> created_fills;

  try {
    for (auto* layer : db_->getTech()->getLayers()) {
      const auto config_it = layers.find(layer);
      if (config_it == layers.end()) {
        continue;
      }
      const FillLayerConfig& config = config_it->second;
      const auto layer_start = Clock::now();
      if (config.has_opc) {
        logger_->warn(FIN,
                      46,
                      "Fixed-dissection LP fill uses non-OPC rules only on "
                      "layer {}; OPC fill is not yet supported.",
                      layer->getConstName());
      }

      lpfill::traceCandidate(std::string("layer=") + layer->getConstName()
                             + " collect_non_fill begin");
      const Polygon90Set layer_shapes = orNonFills(block, layer);
      lpfill::traceCandidate("collect_non_fill end; initial_density begin");
      grid.calculateMetalDensities(layer_shapes);
      lpfill::traceCandidate("initial_density end; candidates begin");
      const auto candidates_start = Clock::now();
      lpfill::traceCandidate("bloat_non_fill begin");
      const auto bloat_start = Clock::now();
      const Polygon90Set bloated_non_fill
          = layer_shapes + config.non_opc.space_to_non_fill;
      const double bloat_seconds = seconds(bloat_start, Clock::now());
      lpfill::traceCandidate("bloat_non_fill end");
      auto layer_fill_areas = lpfill::makeLayerFillAreas(grid,
                                                         bloated_non_fill,
                                                         layer,
                                                         config.non_opc,
                                                         graphics_.get(),
                                                         candidate_threads);
      std::cout << "[Fill LP] candidate preparation layer="
                << layer->getConstName()
                << " candidate_prepare_threads=" << layer_fill_areas.threads
                << std::endl;
      auto candidate_phases = std::move(layer_fill_areas.phase_seconds);
      candidate_phases["candidate_bloat_seconds"] = bloat_seconds;
      const auto& bloated_non_fill_areas
          = layer_fill_areas.bloated_non_fill_areas;

      std::vector<std::vector<Rectangle>> tile_candidates;
      std::vector<Polygon90> fillable_polygons;
      std::vector<double> fillable_region_areas;
      fillable_region_areas.reserve(grid.tiles().size());
      std::vector<double> target_tile_densities;
      std::vector<TileViolation> tile_violations;
      std::vector<WindowViolation> window_violations;
      const size_t layer_fill_start = created_fills.size();
      std::string infeasibility_reason;
      size_t diagnostic_window_index = 0;
      Rect diagnostic_window_bounds;
      double diagnostic_current_density = 0.0;
      double diagnostic_required_fill_area = 0.0;
      double diagnostic_max_legal_fill_area = 0.0;
      double diagnostic_fill_budget = 0.0;
      FixedDissectionLpProblem problem;
      FixedDissectionLpResult result;
      double diagnostic_candidate_seconds = 0.0;
      double candidate_generation_seconds = 0.0;
      double candidate_generation_work_seconds = 0.0;
      int actual_candidate_threads = 1;
      double diagnostic_solve_seconds = 0.0;
      const std::string diagnostics_base
          = lp_diagnostics != nullptr && lp_diagnostics[0] != '\0'
                ? lp_diagnostics
                : "fin_lp_failure";
      problem.diagnostics_prefix
          = diagnostics_base + "_" + layer->getConstName();
      try {
        problem.lp_solver = selected_solver;
        problem.lp_threads = lp_threads;
        const char* solver_log = std::getenv("FIN_LP_SOLVER_LOG");
        problem.solver_log
            = solver_log != nullptr && std::string(solver_log) == "1";
        const char* dual_tolerance = std::getenv("FIN_LP_DUAL_TOLERANCE");
        if (dual_tolerance != nullptr) {
          const std::string value(dual_tolerance);
          size_t consumed = 0;
          problem.dual_tolerance = std::stod(value, &consumed);
          if (consumed != value.size() || !std::isfinite(problem.dual_tolerance)
              || problem.dual_tolerance < 0.0) {
            throw std::invalid_argument("Invalid FIN_LP_DUAL_TOLERANCE.");
          }
        }
        problem.max_window_density = max_window_density;
        problem.min_tile_density = min_tile_density;
        problem.max_tile_density = max_tile_density;
        problem.min_window_density = min_window_density;
        problem.feature_areas = grid.metalAreas();
        problem.windows = grid.windowTileIndices();
        problem.tile_areas.reserve(grid.tiles().size());
        problem.max_fill_areas.reserve(grid.tiles().size());
        auto generated
            = lpfill::makeLayerFillCandidates(layer_fill_areas.tile_areas,
                                              layer,
                                              config.non_opc,
                                              graphics_.get(),
                                              candidate_threads);
        candidate_generation_seconds = generated.wall_seconds;
        candidate_generation_work_seconds = generated.work_seconds;
        actual_candidate_threads = generated.threads;
        tile_candidates = std::move(generated.tiles);
        const auto summary_start = Clock::now();
        for (size_t tile_index = 0; tile_index < grid.tiles().size();
             ++tile_index) {
          const Rect& tile = grid.tiles()[tile_index];
          const auto tile_start = Clock::now();
          if (lpfill::candidateTraceEnabled()) {
            lpfill::traceCandidate(
                std::string("tile summary begin layer=") + layer->getConstName()
                + " index=" + std::to_string(tile_index)
                + " total=" + std::to_string(grid.tiles().size())
                + " bounds_dbu=(" + std::to_string(tile.xMin()) + ","
                + std::to_string(tile.yMin()) + ")-("
                + std::to_string(tile.xMax()) + ","
                + std::to_string(tile.yMax()) + ")");
          }
          Polygon90Set tile_fillable_area
              = std::move(layer_fill_areas.tile_areas[tile_index]);
          const auto& candidates = tile_candidates[tile_index];
          std::vector<Polygon90> tile_fillable_polygons;
          tile_fillable_area.get(tile_fillable_polygons);
          fillable_polygons.insert(fillable_polygons.end(),
                                   tile_fillable_polygons.begin(),
                                   tile_fillable_polygons.end());
          fillable_region_areas.push_back(
              boost::polygon::area(tile_fillable_area));
          double capacity = 0.0;
          for (const Rectangle& candidate : candidates) {
            capacity += static_cast<double>(xh(candidate) - xl(candidate))
                        * (yh(candidate) - yl(candidate));
          }
          problem.tile_areas.push_back(tile.area());
          problem.max_fill_areas.push_back(capacity);
          if (lpfill::candidateTraceEnabled()) {
            lpfill::traceCandidate(
                "tile summary end index=" + std::to_string(tile_index)
                + " candidates=" + std::to_string(candidates.size())
                + " elapsed_seconds="
                + std::to_string(seconds(tile_start, Clock::now())));
          }
        }
        const auto candidates_end = Clock::now();
        candidate_phases["candidate_summary_seconds"]
            = seconds(summary_start, candidates_end);
        candidate_phases["candidate_generation_seconds"]
            = candidate_generation_seconds;
        double candidate_accounted = 0.0;
        for (const auto& [name, value] : candidate_phases) {
          candidate_accounted += value;
        }
        candidate_phases["candidate_other_seconds"] = std::max(
            0.0,
            seconds(candidates_start, candidates_end) - candidate_accounted);
        for (const auto& [name, value] : candidate_phases) {
          std::cout << "[Fill LP] candidate phase layer="
                    << layer->getConstName() << " name=" << name
                    << " seconds=" << value << std::endl;
        }
        diagnostic_candidate_seconds
            = seconds(candidates_start, candidates_end);
        std::cout << "[Fill LP] candidate generation layer="
                  << layer->getConstName()
                  << " tiles=" << tile_candidates.size()
                  << " candidate_generation_seconds="
                  << candidate_generation_seconds
                  << " candidate_generation_work_seconds="
                  << candidate_generation_work_seconds
                  << " candidate_threads=" << actual_candidate_threads
                  << std::endl;
        lpfill::traceCandidate("candidates end");

        size_t omitted_tile_maximums = 0;
        size_t omitted_window_maximums = 0;
        for (size_t tile_index = 0; tile_index < grid.tiles().size();
             tile_index++) {
          if (problem.feature_areas[tile_index]
              > max_tile_density * problem.tile_areas[tile_index]) {
            omitted_tile_maximums++;
          }
          const double required_fill_area
              = std::max(min_tile_density * problem.tile_areas[tile_index]
                             - problem.feature_areas[tile_index],
                         0.0);
          if (required_fill_area > problem.max_fill_areas[tile_index]) {
            const Rect& tile = grid.tiles()[tile_index];
            const double max_tile_density
                = (problem.feature_areas[tile_index]
                   + problem.max_fill_areas[tile_index])
                  / problem.tile_areas[tile_index];
            logger_->warn(
                FIN,
                50,
                "Omitting minimum tile density constraint on layer {}: tile {} "
                "({}, {})-({}, {}) requires minimum density {:.6f}, but its "
                "legal fill capacity reaches only {:.6f}.",
                layer->getConstName(),
                tile_index,
                tile.xMin(),
                tile.yMin(),
                tile.xMax(),
                tile.yMax(),
                min_tile_density,
                max_tile_density);
          }
        }

        for (size_t window_index = 0; window_index < problem.windows.size();
             window_index++) {
          double tile_minimum_fill_area = 0.0;
          double maximum_legal_fill_area = 0.0;
          double window_area = 0.0;
          double feature_area = 0.0;
          for (const size_t tile_index : problem.windows[window_index]) {
            tile_minimum_fill_area += problem.minimumFillArea(tile_index);
            maximum_legal_fill_area += problem.maximumFillArea(tile_index);
            window_area += problem.tile_areas[tile_index];
            feature_area += problem.feature_areas[tile_index];
          }
          if (feature_area > max_window_density * window_area) {
            omitted_window_maximums++;
          }
          const double fill_budget
              = problem.maximumWindowFillArea(window_area, feature_area);
          const double window_minimum_fill_area = std::max(
              problem.min_window_density * window_area - feature_area, 0.0);
          const double required_fill_area
              = std::max(tile_minimum_fill_area, window_minimum_fill_area);
          if (required_fill_area > fill_budget
              || required_fill_area > maximum_legal_fill_area) {
            window_violations.push_back(
                {window_index, required_fill_area, fill_budget});
            if (infeasibility_reason.empty()) {
              const DensityWindow& window = grid.windows()[window_index];
              infeasibility_reason
                  = required_fill_area > fill_budget
                        ? "window_lower_bound_exceeds_upper_bound"
                        : "window_lower_bound_exceeds_legal_capacity";
              diagnostic_window_index = window_index;
              diagnostic_window_bounds = window.bounds;
              diagnostic_current_density = window.density;
              diagnostic_required_fill_area = required_fill_area;
              diagnostic_max_legal_fill_area = maximum_legal_fill_area;
              diagnostic_fill_budget = fill_budget;
            }
          }
        }
        if (omitted_tile_maximums != 0 || omitted_window_maximums != 0) {
          logger_->warn(FIN,
                        70,
                        "Omitting maximum density constraints on layer {}: "
                        "{} tiles and {} windows already exceed their limits "
                        "with existing metal.",
                        layer->getConstName(),
                        omitted_tile_maximums,
                        omitted_window_maximums);
        }
        if (!window_violations.empty()) {
          logger_->error(FIN,
                         51,
                         "Fixed-dissection LP fill is infeasible on layer {}: "
                         "-min_tile_density {:.6f}, -max_tile_density {:.6f}, "
                         "-min_window_density {:.6f}, and "
                         "-max_window_density {:.6f} cannot "
                         "be satisfied simultaneously; {} at window {} "
                         "({}, {})-({}, {}): current_density={:.6f}, "
                         "required_fill={:.0f}, legal_fill_capacity={:.0f}, "
                         "window_fill_budget={:.0f}.",
                         layer->getConstName(),
                         min_tile_density,
                         max_tile_density,
                         min_window_density,
                         max_window_density,
                         infeasibility_reason,
                         diagnostic_window_index,
                         diagnostic_window_bounds.xMin(),
                         diagnostic_window_bounds.yMin(),
                         diagnostic_window_bounds.xMax(),
                         diagnostic_window_bounds.yMax(),
                         diagnostic_current_density,
                         diagnostic_required_fill_area,
                         diagnostic_max_legal_fill_area,
                         diagnostic_fill_budget);
        }

        const auto solve_start = Clock::now();
        result
            = lip_type == 0
                  ? (minimize_total_fill_area ? solveMinFillAmountLp(problem)
                                              : solveMinVarLP(problem))
                  : solveLipLpFill(
                      problem,
                      makeLipLpNeighborhoods(
                          grid, resolution, static_cast<LipLpType>(lip_type)));
        const auto solve_end = Clock::now();
        diagnostic_solve_seconds
            = seconds(solve_start, solve_end) - result.diagnostics_seconds;
        if (lip_type != 0 && result.solved) {
          logger_->info(FIN,
                        65,
                        "Fixed-dissection Lip{} LP objective L={:.6f} on "
                        "layer {}.",
                        lip_type,
                        result.lip_value,
                        layer->getConstName());
        }
        std::cout << "[Fill LP] solver statistics layer="
                  << layer->getConstName()
                  << " solver=" << fillLpSolverName(problem.lp_solver)
                  << " iterations=" << result.solver_iterations
                  << " dual_tolerance=" << problem.dual_tolerance << std::endl;
        if (!result.solved) {
          std::cout << "[Fill LP] failure solver="
                    << fillLpSolverName(problem.lp_solver)
                    << " status=" << result.solver_status
                    << " status_code=" << result.solver_status_code
                    << " diagnostics=" << problem.diagnostics_prefix << ".json"
                    << std::endl;
          logger_->error(FIN,
                         51,
                         "Fixed-dissection LP solve failed on layer {} "
                         "(solver_status={}, code={}): "
                         "-min_tile_density {:.6f}, -max_tile_density {:.6f}, "
                         "-min_window_density {:.6f}, "
                         "-max_window_density {:.6f}. See failure diagnostics.",
                         layer->getConstName(),
                         result.solver_status,
                         result.solver_status_code,
                         min_tile_density,
                         max_tile_density,
                         min_window_density,
                         max_window_density);
        }

        target_tile_densities.reserve(grid.tiles().size());
        for (size_t tile_index = 0; tile_index < grid.tiles().size();
             tile_index++) {
          target_tile_densities.push_back((problem.feature_areas[tile_index]
                                           + result.fill_areas[tile_index])
                                          / problem.tile_areas[tile_index]);
        }

        const auto placement_start_time = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::cout << "[Fill LP] placement start layer=" << layer->getConstName()
                  << " time="
                  << std::put_time(std::localtime(&placement_start_time),
                                   "%Y-%m-%d %H:%M:%S %Z")
                  << " tiles=" << tile_candidates.size() << std::endl;
        const auto placement_start = Clock::now();
        const bool trace_placement = lpfill::candidateTraceEnabled();
        const auto tracePlacement = [&](const std::string& message) {
          if (trace_placement) {
            std::cout << "[Fill placement] layer=" << layer->getConstName()
                      << " elapsed_seconds="
                      << seconds(placement_start, Clock::now()) << " "
                      << message << std::endl;
          }
        };
        tracePlacement("select_candidates begin");
        auto prepared_placement = lpfill::selectLayerFillCandidates(
            tile_candidates,
            result.fill_areas,
            graphics_ ? 1 : placement_threads);
        const auto& placed_areas = prepared_placement.placed_areas;
        const auto& selected_fills = prepared_placement.fills;
        std::cout << "[Fill LP] placement select layer="
                  << layer->getConstName()
                  << " tiles=" << tile_candidates.size()
                  << " placement_select_seconds="
                  << prepared_placement.select_seconds
                  << " placement_select_work_seconds="
                  << prepared_placement.select_work_seconds
                  << " placement_threads=" << prepared_placement.threads
                  << std::endl;
        tracePlacement("select_candidates end");

        // for (size_t tile_index = 0; tile_index < grid.tiles().size();
        //      tile_index++) {
        //   const double placed_density
        //       = (problem.feature_areas[tile_index] +
        //       placed_areas[tile_index])
        //         / problem.tile_areas[tile_index];
        // if (placed_density < min_tile_density) {
        //   const Rect& tile = grid.tiles()[tile_index];
        //   tile_violations.push_back({tile_index,
        //                              TileViolationReason::kDiscreteCandidate,
        //                              min_tile_density,
        //                              placed_density});
        //   logger_->error(
        //       FIN,
        //       52,
        //       "Fixed-dissection LP fill is infeasible on layer {}: tile {} "
        //       "({}, {})-({}, {}) reaches density {:.6f}, below requested "
        //       "minimum density {:.6f} with the available fill candidates.",
        //       layer->getConstName(),
        //       tile_index,
        //       tile.xMin(),
        //       tile.yMin(),
        //       tile.xMax(),
        //       tile.yMax(),
        //       placed_density,
        //       min_tile_density);
        // }
        // }

        const int mask_count = std::max(config.num_masks, 1);
        int mask_index = 0;
        tracePlacement("copy_layer_shapes begin");
        const auto copy_start = Clock::now();
        Polygon90Set post_fill_shapes = layer_shapes;
        const double placement_copy_seconds = seconds(copy_start, Clock::now());
        Polygon90Set selected_fill_shapes
            = std::move(prepared_placement.selected_shapes);
        const auto union_start = Clock::now();
        post_fill_shapes += selected_fill_shapes;
        const double placement_union_seconds
            = seconds(union_start, Clock::now());
        tracePlacement("copy_layer_shapes end; insert_fills begin");
        const auto db_insert_start = Clock::now();
        size_t fill_index = 0;
        for (const Rectangle& fill : selected_fills) {
          if (trace_placement) {
            tracePlacement("fill begin index=" + std::to_string(fill_index)
                           + " total=" + std::to_string(selected_fills.size())
                           + " bounds_dbu=(" + std::to_string(xl(fill)) + ","
                           + std::to_string(yl(fill)) + ")-("
                           + std::to_string(xh(fill)) + ","
                           + std::to_string(yh(fill)) + ")");
          }
          const int mask = mask_count == 1 ? 0 : mask_index++ % mask_count + 1;
          tracePlacement("dbFill.create begin");
          created_fills.push_back(dbFill::create(block,
                                                 false,
                                                 mask,
                                                 layer,
                                                 xl(fill),
                                                 yl(fill),
                                                 xh(fill),
                                                 yh(fill)));
          tracePlacement("dbFill.create end; fill end");
          ++fill_index;
        }
        const double db_insert_seconds = seconds(db_insert_start, Clock::now());
        tracePlacement("insert_fills end; calculate_metal_densities begin");

        std::map<std::string, double> density_phases;
        const auto post_fill_density_start = Clock::now();
        grid.calculateMetalDensities(post_fill_shapes, &density_phases);
        const double post_fill_density_seconds
            = seconds(post_fill_density_start, Clock::now());
        tracePlacement("calculate_metal_densities end");
        const auto placement_end = Clock::now();
        auto phase_seconds = std::move(prepared_placement.phase_seconds);
        phase_seconds["placement_copy_seconds"] = placement_copy_seconds;
        phase_seconds["placement_union_seconds"] = placement_union_seconds;
        phase_seconds["db_insert_seconds"] = db_insert_seconds;
        double density_accounted = 0.0;
        for (const auto& [name, value] : density_phases) {
          density_accounted += value;
          phase_seconds[name] = value;
        }
        phase_seconds["post_fill_density_other_seconds"]
            = std::max(0.0, post_fill_density_seconds - density_accounted);
        phase_seconds["placement_other_seconds"]
            = std::max(0.0,
                       seconds(placement_start, placement_end)
                           - prepared_placement.wall_seconds
                           - placement_copy_seconds - placement_union_seconds
                           - db_insert_seconds - post_fill_density_seconds);
        std::cout << "[Fill LP] placement breakdown layer="
                  << layer->getConstName() << " placement_prepare_seconds="
                  << prepared_placement.wall_seconds
                  << " placement_prepare_work_seconds="
                  << prepared_placement.work_seconds
                  << " db_insert_seconds=" << db_insert_seconds
                  << " post_fill_density_seconds=" << post_fill_density_seconds
                  << " placement_threads=" << prepared_placement.threads
                  << std::endl;
        const auto placement_end_time = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::cout << "[Fill LP] placement end layer=" << layer->getConstName()
                  << " time="
                  << std::put_time(std::localtime(&placement_end_time),
                                   "%Y-%m-%d %H:%M:%S %Z")
                  << " placement_seconds="
                  << seconds(placement_start, placement_end)
                  << " fill_rectangles=" << selected_fills.size() << std::endl;
        for (const auto& [name, value] : phase_seconds) {
          std::cout << "[Fill LP] placement phase layer="
                    << layer->getConstName() << " name=" << name
                    << " seconds=" << value << std::endl;
        }
        const auto [min_post_fill_density, max_post_fill_density]
            = getWindowDensityRange(grid.windows());
        const double planned_area = std::accumulate(
            result.fill_areas.begin(), result.fill_areas.end(), 0.0);
        const double placed_area
            = std::accumulate(placed_areas.begin(), placed_areas.end(), 0.0);
        double min_post_fill_tile_density = 1.0;
        double max_post_fill_tile_density = 0.0;
        size_t min_violation_tile_count = 0;
        size_t max_violation_tile_count = 0;
        for (size_t tile_index = 0; tile_index < grid.tiles().size();
             tile_index++) {
          const double tile_area = problem.tile_areas[tile_index];
          const double target_density = target_tile_densities[tile_index];
          const double actual_density
              = grid.metalAreas()[tile_index] / tile_area;
          min_post_fill_tile_density
              = std::min(min_post_fill_tile_density, actual_density);
          max_post_fill_tile_density
              = std::max(max_post_fill_tile_density, actual_density);
          min_violation_tile_count += actual_density < min_tile_density;
          max_violation_tile_count += actual_density > max_tile_density;
          const double shortage
              = std::max(target_density - actual_density, 0.0);
          logger_->info(FIN,
                        53,
                        "Fixed-dissection LP tile: layer={}, tile={}, "
                        "target={:.6f}, actual={:.6f}, shortage={:.6f}.",
                        layer->getConstName(),
                        tile_index,
                        target_density,
                        actual_density,
                        shortage);
        }
        logger_->info(
            FIN,
            48,
            "Fixed-dissection LP fill: layer={}, planned_fill_area={:.0f} "
            "DBU^2, placed_fill_area={:.0f} DBU^2, fill_rectangles={}, "
            "min_post_fill_density={:.6f}, "
            "max_post_fill_density={:.6f}.",
            layer->getConstName(),
            planned_area,
            placed_area,
            created_fills.size() - layer_fill_start,
            min_post_fill_density,
            max_post_fill_density);
        const auto layer_compute_end = Clock::now();
        const double candidate_seconds
            = seconds(candidates_start, candidates_end);
        const double solve_seconds = seconds(solve_start, solve_end);
        const double placement_seconds
            = seconds(placement_start, placement_end);
        const double layer_compute_seconds
            = seconds(layer_start, layer_compute_end);
        const WindowDensitySummary density_summary = summarizeWindowDensities(
            grid.windows(), min_window_density, max_window_density);
        logger_->info(FIN,
                      54,
                      "Fixed-dissection LP density: layer={}, windows={}, "
                      "min={:.6f}, max={:.6f}, mean={:.6f}, "
                      "variance={:.8f}, min_window_density_limit={:.6f}, "
                      "min_violation_windows={}, "
                      "max_window_density_limit={:.6f}, "
                      "max_violation_windows={}.",
                      layer->getConstName(),
                      grid.windows().size(),
                      min_post_fill_density * 100.0,
                      max_post_fill_density * 100.0,
                      density_summary.mean_density * 100.0,
                      density_summary.variance,
                      min_window_density,
                      density_summary.min_violation_count,
                      max_window_density,
                      density_summary.max_violation_count);
        logger_->info(FIN,
                      55,
                      "Fixed-dissection LP runtime (SVG excluded): "
                      "layer={}, candidates={:.3f}s, lp_solve={:.3f}s, "
                      "placement={:.3f}s, compute={:.3f}s.",
                      layer->getConstName(),
                      candidate_seconds,
                      solve_seconds,
                      placement_seconds,
                      layer_compute_seconds);
        density_reports.push_back({layer->getConstName(),
                                   planned_area,
                                   placed_area,
                                   grid.windows().size(),
                                   min_post_fill_density,
                                   max_post_fill_density,
                                   density_summary.mean_density,
                                   density_summary.variance,
                                   density_summary.min_violation_count,
                                   density_summary.max_violation_count,
                                   makeWindowDensityHistogram(grid.windows()),
                                   true,
                                   "",
                                   0,
                                   Rect(),
                                   0.0,
                                   0.0,
                                   0.0,
                                   0.0,
                                   0,
                                   0,
                                   candidate_seconds,
                                   solve_seconds,
                                   placement_seconds,
                                   layer_compute_seconds,
                                   0.0,
                                   result.solver_status,
                                   result.solver_status_code,
                                   ""});
        density_reports.back().candidate_generation_seconds
            = candidate_generation_seconds;
        density_reports.back().candidate_generation_work_seconds
            = candidate_generation_work_seconds;
        density_reports.back().lp_solver = fillLpSolverName(problem.lp_solver);
        density_reports.back().solver_iterations = result.solver_iterations;
        density_reports.back().candidate_threads = actual_candidate_threads;
        density_reports.back().candidate_prepare_threads
            = layer_fill_areas.threads;
        density_reports.back().candidate_phase_seconds = candidate_phases;
        density_reports.back().placement_prepare_seconds
            = prepared_placement.wall_seconds;
        density_reports.back().placement_prepare_work_seconds
            = prepared_placement.work_seconds;
        density_reports.back().db_insert_seconds = db_insert_seconds;
        density_reports.back().post_fill_density_seconds
            = post_fill_density_seconds;
        density_reports.back().placement_threads = prepared_placement.threads;
        density_reports.back().placement_select_seconds
            = prepared_placement.select_seconds;
        density_reports.back().placement_select_work_seconds
            = prepared_placement.select_work_seconds;
        density_reports.back().placement_phase_seconds
            = std::move(phase_seconds);
        density_reports.back().min_post_fill_tile_density
            = min_post_fill_tile_density;
        density_reports.back().max_post_fill_tile_density
            = max_post_fill_tile_density;
        density_reports.back().min_violation_tile_count
            = min_violation_tile_count;
        density_reports.back().max_violation_tile_count
            = max_violation_tile_count;
        if (svg_filename != nullptr && svg_filename[0] != '\0') {
          compute_seconds += seconds(compute_slice_start, Clock::now());
          const auto svg_start = Clock::now();
          const std::string layer_svg
              = std::string(svg_filename) + "_" + layer->getConstName();
          writeTileGridSvg(grid,
                           layer_svg + "_fillable.svg",
                           layer_shapes,
                           db_->getTech()->getDbUnitsPerMicron(),
                           nullptr,
                           true,
                           nullptr,
                           &fillable_polygons,
                           &target_tile_densities,
                           nullptr,
                           nullptr,
                           &bloated_non_fill_areas,
                           &fillable_region_areas);
          writeTileGridSvg(grid,
                           layer_svg + ".svg",
                           layer_shapes,
                           db_->getTech()->getDbUnitsPerMicron(),
                           nullptr,
                           true,
                           &selected_fill_shapes,
                           &fillable_polygons,
                           &target_tile_densities,
                           &tile_violations,
                           &window_violations,
                           &bloated_non_fill_areas,
                           &fillable_region_areas);
          const double layer_svg_seconds = seconds(svg_start, Clock::now());
          density_reports.back().svg_seconds = layer_svg_seconds;
          svg_seconds += layer_svg_seconds;
          compute_slice_start = Clock::now();
        }
        total_placed_area += placed_area;
      } catch (const std::runtime_error& error) {
        const std::string error_id(error.what());
        if (error_id != "FIN-0050" && error_id != "FIN-0051"
            && error_id != "FIN-0052") {
          throw;
        }
        for (size_t index = layer_fill_start; index < created_fills.size();
             index++) {
          dbFill::destroy(created_fills[index]);
        }
        created_fills.resize(layer_fill_start);
        const auto diagnostics_start = Clock::now();
        writeFillLpFailureDiagnostics(problem,
                                      grid,
                                      layer->getConstName(),
                                      result,
                                      min_window_density,
                                      infeasibility_reason);
        const double diagnostics_seconds
            = result.diagnostics_seconds
              + seconds(diagnostics_start, Clock::now());
        std::cout << "[Fill LP] failure runtime layer=" << layer->getConstName()
                  << " candidates=" << diagnostic_candidate_seconds
                  << " lp_model_and_solve=" << diagnostic_solve_seconds
                  << " diagnostics=" << diagnostics_seconds
                  << " placement=not_started" << std::endl;
        std::cout << "[Fill LP] failure diagnostics written: "
                  << problem.diagnostics_prefix << ".json" << std::endl;
        const WindowDensitySummary density_summary = summarizeWindowDensities(
            grid.windows(), min_window_density, max_window_density);
        density_reports.push_back(
            {layer->getConstName(),
             0.0,
             0.0,
             grid.windows().size(),
             density_summary.min_density,
             density_summary.max_density,
             density_summary.mean_density,
             density_summary.variance,
             density_summary.min_violation_count,
             density_summary.max_violation_count,
             makeWindowDensityHistogram(grid.windows()),
             false,
             infeasibility_reason.empty()
                 ? (result.solver_status_code == 2 ? "lp_infeasible"
                                                   : "lp_solver_failure")
                 : infeasibility_reason,
             diagnostic_window_index,
             diagnostic_window_bounds,
             diagnostic_current_density,
             diagnostic_required_fill_area,
             diagnostic_max_legal_fill_area,
             diagnostic_fill_budget,
             tile_violations.size(),
             window_violations.size(),
             diagnostic_candidate_seconds,
             diagnostic_solve_seconds,
             0.0,
             seconds(layer_start, Clock::now()),
             0.0,
             result.solver_status,
             result.solver_status_code,
             problem.diagnostics_prefix,
             diagnostics_seconds});
        density_reports.back().candidate_generation_seconds
            = candidate_generation_seconds;
        density_reports.back().candidate_generation_work_seconds
            = candidate_generation_work_seconds;
        density_reports.back().lp_solver = fillLpSolverName(problem.lp_solver);
        density_reports.back().solver_iterations = result.solver_iterations;
        density_reports.back().candidate_threads = actual_candidate_threads;
        density_reports.back().candidate_prepare_threads
            = layer_fill_areas.threads;
        density_reports.back().candidate_phase_seconds = candidate_phases;
        if (svg_filename != nullptr && svg_filename[0] != '\0') {
          compute_seconds += seconds(compute_slice_start, Clock::now());
          const auto svg_start = Clock::now();
          const std::string layer_svg
              = std::string(svg_filename) + "_" + layer->getConstName();
          writeTileGridSvg(
              grid,
              layer_svg + "_fillable.svg",
              layer_shapes,
              db_->getTech()->getDbUnitsPerMicron(),
              nullptr,
              true,
              nullptr,
              &fillable_polygons,
              target_tile_densities.empty() ? nullptr : &target_tile_densities,
              &tile_violations,
              &window_violations,
              &bloated_non_fill_areas,
              &fillable_region_areas);
          Polygon90Set no_fill_shapes;
          writeTileGridSvg(
              grid,
              layer_svg + ".svg",
              layer_shapes,
              db_->getTech()->getDbUnitsPerMicron(),
              nullptr,
              true,
              &no_fill_shapes,
              &fillable_polygons,
              target_tile_densities.empty() ? nullptr : &target_tile_densities,
              &tile_violations,
              &window_violations,
              &bloated_non_fill_areas,
              &fillable_region_areas);
          svg_seconds += seconds(svg_start, Clock::now());
          compute_slice_start = Clock::now();
        }
        continue;
      }
    }
    compute_seconds += seconds(compute_slice_start, Clock::now());
    logger_->info(FIN,
                  56,
                  "Fixed-dissection LP total runtime (SVG excluded): "
                  "{:.3f}s; SVG rendering: {:.3f}s.",
                  compute_seconds,
                  svg_seconds);
    writeLPFillDensityReport(density_report_filename,
                             density_reports,
                             compute_seconds,
                             svg_seconds,
                             min_window_density,
                             max_window_density,
                             logger_);
    return total_placed_area;
  } catch (...) {
    for (dbFill* fill : created_fills) {
      dbFill::destroy(fill);
    }
    throw;
  }
}

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "LPFill.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
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
                      const FillMethod method)
{
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

      const Polygon90Set layer_shapes = orNonFills(block, layer);
      grid.calculateMetalDensities(layer_shapes);
      const auto candidates_start = Clock::now();
      const Polygon90Set bloated_non_fill
          = layer_shapes + config.non_opc.space_to_non_fill;
      std::vector<double> bloated_non_fill_areas;
      bloated_non_fill_areas.reserve(grid.tiles().size());
      for (const Rect& tile : grid.tiles()) {
        Polygon90Set tile_polygon;
        tile_polygon
            += makeRect(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax());
        bloated_non_fill_areas.push_back(
            boost::polygon::area(bloated_non_fill & tile_polygon));
      }

      std::vector<std::vector<Rectangle>> tile_candidates;
      tile_candidates.reserve(grid.tiles().size());
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
      try {
        FixedDissectionLpProblem problem;
        problem.max_window_density = max_window_density;
        problem.min_tile_density = min_tile_density;
        problem.max_tile_density = max_tile_density;
        // Placement rounds a continuous LP area down to legal rectangles.
        // Keep a small guard only for the minimum-amount objective so the
        // placed, rather than merely planned, fill meets the user limit.
        problem.min_window_density
            = minimize_total_fill_area
                  ? std::min(min_window_density + 5e-4, max_window_density)
                  : min_window_density;
        problem.feature_areas = grid.metalAreas();
        problem.windows = grid.windowTileIndices();
        problem.tile_areas.reserve(grid.tiles().size());
        problem.max_fill_areas.reserve(grid.tiles().size());
        for (const Rect& tile : grid.tiles()) {
          Polygon90Set tile_fillable_area;
          std::vector<Rectangle> candidates
              = lpfill::makeTileFillCandidates(tile,
                                               layer_shapes,
                                               layer,
                                               config.non_opc,
                                               graphics_.get(),
                                               &tile_fillable_area);
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
          tile_candidates.push_back(std::move(candidates));
          problem.tile_areas.push_back(tile.area());
          problem.max_fill_areas.push_back(capacity);
        }
        const auto candidates_end = Clock::now();

        for (size_t tile_index = 0; tile_index < grid.tiles().size();
             tile_index++) {
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
            tile_violations.push_back({tile_index,
                                       TileViolationReason::kCapacity,
                                       min_tile_density,
                                       max_tile_density});
            logger_->error(
                FIN,
                50,
                "Fixed-dissection LP fill is infeasible on layer {}: tile {} "
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
            tile_minimum_fill_area
                += std::max(min_tile_density * problem.tile_areas[tile_index]
                                - problem.feature_areas[tile_index],
                            0.0);
            const double max_tile_fill_area
                = max_tile_density * problem.tile_areas[tile_index]
                  - problem.feature_areas[tile_index];
            maximum_legal_fill_area
                += std::max(std::min(problem.max_fill_areas[tile_index],
                                     max_tile_fill_area),
                            0.0);
            window_area += problem.tile_areas[tile_index];
            feature_area += problem.feature_areas[tile_index];
          }
          const double fill_budget
              = std::max(max_window_density * window_area - feature_area, 0.0);
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
        const FixedDissectionLpResult result
            = lip_type == 0
                  ? (minimize_total_fill_area ? solveMinFillAmountLp(problem)
                                              : solveMinVarLP(problem))
                  : solveLipLpFill(
                      problem,
                      makeLipLpNeighborhoods(
                          grid, resolution, static_cast<LipLpType>(lip_type)));
        const auto solve_end = Clock::now();
        if (lip_type != 0 && result.solved) {
          logger_->info(FIN,
                        65,
                        "Fixed-dissection Lip{} LP objective L={:.6f} on "
                        "layer {}.",
                        lip_type,
                        result.lip_value,
                        layer->getConstName());
        }
        if (!result.solved) {
          logger_->error(FIN,
                         51,
                         "Fixed-dissection LP fill is infeasible on layer {}: "
                         "-min_tile_density {:.6f}, -max_tile_density {:.6f}, "
                         "-min_window_density {:.6f}, and "
                         "-max_window_density {:.6f} cannot "
                         "be satisfied simultaneously.",
                         layer->getConstName(),
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

        const auto placement_start = Clock::now();
        std::vector<double> placed_areas(grid.tiles().size(), 0.0);
        std::vector<Rectangle> selected_fills;
        for (size_t tile_index = 0; tile_index < tile_candidates.size();
             tile_index++) {
          const double target_area = result.fill_areas[tile_index];
          for (const Rectangle& candidate : tile_candidates[tile_index]) {
            const double candidate_area
                = static_cast<double>(xh(candidate) - xl(candidate))
                  * (yh(candidate) - yl(candidate));
            if (placed_areas[tile_index] + candidate_area <= target_area) {
              selected_fills.push_back(candidate);
              placed_areas[tile_index] += candidate_area;
            }
          }
        }

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
        Polygon90Set post_fill_shapes = layer_shapes;
        Polygon90Set selected_fill_shapes;
        for (const Rectangle& fill : selected_fills) {
          const int mask = mask_count == 1 ? 0 : mask_index++ % mask_count + 1;
          created_fills.push_back(dbFill::create(block,
                                                 false,
                                                 mask,
                                                 layer,
                                                 xl(fill),
                                                 yl(fill),
                                                 xh(fill),
                                                 yh(fill)));
          const Polygon90 fill_shape
              = makeRect(xl(fill), yl(fill), xh(fill), yh(fill));
          post_fill_shapes += fill_shape;
          selected_fill_shapes += fill_shape;
        }

        grid.calculateMetalDensities(post_fill_shapes);
        const auto placement_end = Clock::now();
        const auto [min_post_fill_density, max_post_fill_density]
            = getWindowDensityRange(grid.windows());
        const double planned_area = std::accumulate(
            result.fill_areas.begin(), result.fill_areas.end(), 0.0);
        const double placed_area
            = std::accumulate(placed_areas.begin(), placed_areas.end(), 0.0);
        for (size_t tile_index = 0; tile_index < grid.tiles().size();
             tile_index++) {
          const double tile_area = problem.tile_areas[tile_index];
          const double target_density = target_tile_densities[tile_index];
          const double actual_density
              = grid.metalAreas()[tile_index] / tile_area;
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
                                   0.0});
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
        const WindowDensitySummary density_summary = summarizeWindowDensities(
            grid.windows(), min_window_density, max_window_density);
        density_reports.push_back({layer->getConstName(),
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
                                       ? "lp_infeasible"
                                       : infeasibility_reason,
                                   diagnostic_window_index,
                                   diagnostic_window_bounds,
                                   diagnostic_current_density,
                                   diagnostic_required_fill_area,
                                   diagnostic_max_legal_fill_area,
                                   diagnostic_fill_budget,
                                   tile_violations.size(),
                                   window_violations.size(),
                                   0.0,
                                   0.0,
                                   0.0,
                                   seconds(layer_start, Clock::now()),
                                   0.0});
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

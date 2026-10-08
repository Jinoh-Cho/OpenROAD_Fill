// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "MinVarFill.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "FillConfig.h"
#include "FillGeometry.h"
#include "FillUtill.h"
#include "FixedDissectionLp.h"
#include "LipLpFill.h"
#include "MLFixedDissectionLp.h"
#include "graphics.h"
#include "polygon.h"
#include "utl/timer.h"

namespace fin {

using utl::FIN;

using odb::dbBlock;
using odb::dbDatabase;
using odb::dbFill;
using odb::dbTech;
using odb::dbTechLayer;
using odb::dbTechLayerDir;
using odb::Rect;

namespace {

struct WindowDensitySummary
{
  double min_density = 0.0;
  double max_density = 0.0;
  double mean_density = 0.0;
  double variance = 0.0;
  size_t min_violation_count = 0;
  size_t max_violation_count = 0;
};

constexpr size_t kWindowDensityHistogramBins = 20;
using WindowDensityHistogram = std::array<size_t, kWindowDensityHistogramBins>;

WindowDensityHistogram makeWindowDensityHistogram(
    const std::vector<DensityWindow>& windows)
{
  WindowDensityHistogram histogram{};
  for (const DensityWindow& window : windows) {
    const double density = std::clamp(window.density, 0.0, 1.0);
    const size_t bin
        = std::min(static_cast<size_t>(density * kWindowDensityHistogramBins),
                   kWindowDensityHistogramBins - 1);
    histogram[bin]++;
  }
  return histogram;
}

void writeWindowDensityHistogram(std::ostream& report,
                                 const WindowDensityHistogram& histogram)
{
  report << ", \"window_density_histogram\": {\"bin_width\": "
         << 1.0 / kWindowDensityHistogramBins << ", \"counts\": [";
  for (size_t bin = 0; bin < histogram.size(); bin++) {
    report << histogram[bin] << (bin + 1 == histogram.size() ? "]}" : ", ");
  }
}

WindowDensitySummary summarizeWindowDensities(
    const std::vector<DensityWindow>& windows,
    double min_density_limit,
    double max_density_limit)
{
  WindowDensitySummary summary;
  if (windows.empty()) {
    return summary;
  }

  summary.min_density = windows.front().density;
  summary.max_density = windows.front().density;
  for (const DensityWindow& window : windows) {
    summary.min_density = std::min(summary.min_density, window.density);
    summary.max_density = std::max(summary.max_density, window.density);
    summary.mean_density += window.density;
    summary.min_violation_count += min_density_limit >= 0.0
                                   && window.density < min_density_limit - 1e-9;
    summary.max_violation_count += max_density_limit >= 0.0
                                   && window.density > max_density_limit + 1e-9;
  }
  summary.mean_density /= windows.size();
  for (const DensityWindow& window : windows) {
    const double delta = window.density - summary.mean_density;
    summary.variance += delta * delta;
  }
  summary.variance /= windows.size();
  return summary;
}

}  // namespace

std::pair<int, int> MinVarFill::getSpacing(dbTechLayer* layer,
                                           const FillShapesConfig& cfg)
{
  const bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  int space_x = cfg.space_to_fill;
  int space_y = space_x;
  if (is_horiz) {
    space_x = std::max(space_x, cfg.space_line_end);
  } else {
    space_y = std::max(space_y, cfg.space_line_end);
  }
  return std::make_pair(space_x, space_y);
}

void MinVarFill::prune(Polygon90Set& fill_area,
                       dbTechLayer* layer,
                       const FillShapesConfig& cfg,
                       Graphics* graphics)
{
  static_cast<void>(graphics);
  const auto [space_x, space_y] = getSpacing(layer, cfg);
  Polygon90Set pruned(fill_area);
  grow_and(pruned, space_x, space_x, space_y, space_y);
  fill_area -= pruned;
}

void MinVarFill::fillPolygon(const Polygon90& area,
                             dbTechLayer* layer,
                             dbBlock* block,
                             const FillShapesConfig& config,
                             int num_masks,
                             bool needs_opc,
                             Graphics* graphics,
                             Polygon90Set* filled_area)
{
  const std::vector<Rectangle> rectangles
      = makeFillCandidates(area, layer, config, graphics);
  const int mask_count = std::max(num_masks, 1);
  int index = 0;
  for (const Rectangle& rectangle : rectangles) {
    const int mask = mask_count == 1 ? 0 : index++ % mask_count + 1;
    dbFill::create(block,
                   needs_opc,
                   mask,
                   layer,
                   xl(rectangle),
                   yl(rectangle),
                   xh(rectangle),
                   yh(rectangle));
    if (filled_area != nullptr) {
      *filled_area += makeRect(
          xl(rectangle), yl(rectangle), xh(rectangle), yh(rectangle));
    }
  }
}

std::vector<Rectangle> MinVarFill::makeFillCandidates(
    const Polygon90& area,
    dbTechLayer* layer,
    const FillShapesConfig& config,
    Graphics* graphics)
{
  Polygon90Set fill_area;
  fill_area += area;
  const bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  const auto [space_x, space_y] = getSpacing(layer, config);
  std::vector<Rectangle> candidates;
  auto iter = config.shapes.begin();
  while (iter != config.shapes.end()) {
    auto [width, height] = *iter++;
    if ((is_horiz && width < height) || (!is_horiz && height < width)) {
      std::swap(width, height);
    }
    Polygon90Set pruned_fill_area = fill_area;
    const int east_west_sizing = width / 2 - 1;
    const int north_south_sizing = height / 2 - 1;
    shrink(pruned_fill_area,
           east_west_sizing,
           east_west_sizing,
           north_south_sizing,
           north_south_sizing);
    bloat(pruned_fill_area,
          east_west_sizing,
          east_west_sizing,
          north_south_sizing,
          north_south_sizing);
    prune(pruned_fill_area, layer, config, graphics);
    if (graphics != nullptr) {
      graphics->status("Fill Area for " + std::to_string(width) + " "
                       + std::to_string(height));
      graphics->drawPolygon90Set(pruned_fill_area);
    }

    Polygon90Set all_shape_fills;
    std::vector<Polygon90> sub_fill_areas;
    pruned_fill_area.get(sub_fill_areas);
    for (const auto& sub_fill_area : sub_fill_areas) {
      Rectangle bounds;
      extents(bounds, sub_fill_area);
      Polygon90Set tiled_fills;
      for (int x = xl(bounds); x < xh(bounds); x += width + space_x) {
        for (int y = yl(bounds); y < yh(bounds); y += height + space_y) {
          tiled_fills.insert(makeRect(x, y, x + width, y + height));
        }
      }
      Polygon90Set fills = tiled_fills & sub_fill_area;
      keep(fills,
           width * height,
           width * height,
           width - 1,
           width,
           height - 1,
           height);
      Polygon90Set bloated_fills(fills);
      all_shape_fills
          += bloat(bloated_fills, space_x, space_x, space_y, space_y);

      std::vector<Rectangle> rectangles;
      fills.get_rectangles(rectangles);
      candidates.insert(candidates.end(), rectangles.begin(), rectangles.end());
    }
    fill_area -= all_shape_fills;
  }
  return candidates;
}

std::vector<Rectangle> MinVarFill::makeTileFillCandidates(
    const Rect& tile,
    const Polygon90Set& non_fill,
    dbTechLayer* layer,
    const FillShapesConfig& config,
    Graphics* graphics,
    Polygon90Set* fillable_area)
{
  Polygon90Set tile_area
      = makeTileFillArea(tile, non_fill, layer, config, graphics);
  if (fillable_area != nullptr) {
    *fillable_area = tile_area;
  }

  std::vector<Polygon90> polygons;
  tile_area.get(polygons);
  std::vector<Rectangle> candidates;
  for (const Polygon90& polygon : polygons) {
    std::vector<Rectangle> polygon_candidates
        = makeFillCandidates(polygon, layer, config, graphics);
    candidates.insert(
        candidates.end(), polygon_candidates.begin(), polygon_candidates.end());
  }
  return candidates;
}

Polygon90Set MinVarFill::makeTileFillArea(const Rect& tile,
                                          const Polygon90Set& non_fill,
                                          dbTechLayer* layer,
                                          const FillShapesConfig& config,
                                          Graphics* graphics)
{
  const auto [space_x, space_y] = getSpacing(layer, config);
  Polygon90Set tile_area;
  tile_area += makeRect(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax());

  // Keep fills from adjacent tiles apart without needing cross-tile conflicts.
  shrink(tile_area, space_x, space_x, space_y, space_y);
  tile_area -= non_fill + config.space_to_non_fill;
  prune(tile_area, layer, config, graphics);
  return tile_area;
}

void MinVarFill::fillLayer(dbBlock* block,
                           dbTechLayer* layer,
                           const Rect& fill_bounds_rect)
{
  logger_->info(FIN, 22, "MinVar filling layer {}.", layer->getConstName());
  const Polygon90Set non_fill = orNonFills(block, layer);
  const Polygon90 fill_bounds = makeRect(fill_bounds_rect.xMin(),
                                         fill_bounds_rect.yMin(),
                                         fill_bounds_rect.xMax(),
                                         fill_bounds_rect.yMax());
  const FillLayerConfig& cfg = layers_[layer];
  Polygon90Set fill_area
      = fill_bounds - (non_fill + cfg.non_opc.space_to_non_fill);
  if (graphics_) {
    graphics_->status("Non-OPC Area");
    graphics_->drawPolygon90Set(fill_area);
  }
  prune(fill_area, layer, cfg.non_opc, graphics_.get());
  std::vector<Polygon90> polygons;
  fill_area.get(polygons);
  logger_->info(
      FIN, 24, "MinVar filling {} areas with non-OPC fill.", polygons.size());
  Polygon90Set non_opc_fill_area;
  for (const auto& polygon : polygons) {
    fillPolygon(polygon,
                layer,
                block,
                cfg.non_opc,
                cfg.num_masks,
                false,
                graphics_.get(),
                &non_opc_fill_area);
  }
  logger_->info(FIN, 25, "MinVar total fills: {}.", block->getFills().size());
  if (!cfg.has_opc) {
    return;
  }
  Polygon90Set opc_area = fill_bounds - (non_fill + cfg.opc.space_to_non_fill)
                          - (non_opc_fill_area + cfg.non_opc.space_to_fill);
  if (graphics_) {
    graphics_->status("OPC Area");
    graphics_->drawPolygon90Set(opc_area);
  }
  prune(opc_area, layer, cfg.opc, graphics_.get());
  polygons.clear();
  opc_area.get(polygons);
  logger_->info(
      FIN, 26, "MinVar filling {} areas with OPC fill.", polygons.size());
  for (const auto& polygon : polygons) {
    fillPolygon(
        polygon, layer, block, cfg.opc, cfg.num_masks, true, graphics_.get());
  }
  logger_->info(FIN, 27, "MinVar total fills: {}.", block->getFills().size());
  if (graphics_) {
    graphics_->status("OPC Area");
    graphics_->drawPolygon90Set(opc_area);
  }
}

bool MinVarFill::writeFillAreaSvg(const std::string& filename,
                                  const Polygon90Set& fill_area,
                                  const Polygon90Set& non_fill,
                                  const Rect& bounds)
{
  std::vector<Rectangle> fill_rectangles;
  get_rectangles(fill_rectangles, fill_area);
  std::vector<Rectangle> non_fill_rectangles;
  get_rectangles(non_fill_rectangles, non_fill);
  std::ofstream svg(filename);
  if (!svg) {
    return false;
  }
  const int width = bounds.dx();
  const int height = bounds.dy();
  const int outline_width = std::max(1, std::min(width, height) / 600);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << bounds.xMin()
      << ' ' << bounds.yMin() << ' ' << width << ' ' << height << "\">\n";
  svg << "  <rect x=\"" << bounds.xMin() << "\" y=\"" << bounds.yMin()
      << "\" width=\"" << width << "\" height=\"" << height
      << "\" fill=\"white\" stroke=\"black\"/>\n";
  svg << "  <g transform=\"translate(0 " << bounds.yMin() + bounds.yMax()
      << ") scale(1 -1)\" fill=\"#4e79a7\" fill-opacity=\"0.75\" "
         "stroke=\"#1f4e79\" vector-effect=\"non-scaling-stroke\">\n";
  for (const auto& rect : non_fill_rectangles) {
    svg << "    <rect x=\"" << xl(rect) << "\" y=\"" << yl(rect)
        << "\" width=\"" << xh(rect) - xl(rect) << "\" height=\""
        << yh(rect) - yl(rect) << "\"/>\n";
  }
  svg << "  </g>\n";
  const std::array<const char*, 6> colors
      = {"#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#a65628"};
  svg << "  <g transform=\"translate(0 " << bounds.yMin() + bounds.yMax()
      << ") scale(1 -1)\" fill=\"#ffd400\" fill-opacity=\"0.55\" "
      << "stroke-width=\"" << outline_width << "\">\n";
  for (size_t index = 0; index < fill_rectangles.size(); index++) {
    const auto& rect = fill_rectangles[index];
    svg << "    <rect x=\"" << xl(rect) << "\" y=\"" << yl(rect)
        << "\" width=\"" << xh(rect) - xl(rect) << "\" height=\""
        << yh(rect) - yl(rect) << "\" stroke=\""
        << colors[index % colors.size()] << "\"/>\n";
  }
  svg << "  </g>\n</svg>\n";
  return svg.good();
}

bool MinVarFill::writeMLTileDensitySvg(
    const std::string& filename,
    const TileGrid& grid,
    const MultilevelDensityAnalysisResult& analysis,
    const Polygon90Set& metal_shapes,
    const Rect& bounds)
{
  if (grid.tiles().size() != grid.metalAreas().size()) {
    return false;
  }
  std::ofstream svg(filename);
  if (!svg) {
    return false;
  }
  const int width = bounds.dx();
  const int height = bounds.dy();
  const int stroke_width = std::max(1, std::min(width, height) / 500);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << bounds.xMin()
      << ' ' << bounds.yMin() << ' ' << width << ' ' << height << "\">\n";
  svg << "  <rect x=\"" << bounds.xMin() << "\" y=\"" << bounds.yMin()
      << "\" width=\"" << width << "\" height=\"" << height
      << "\" fill=\"white\" stroke=\"black\"/>\n";
  std::vector<Rectangle> metal_rectangles;
  get_rectangles(metal_rectangles, metal_shapes);
  svg << "  <g fill=\"#555555\" fill-opacity=\"0.55\">\n";
  for (const Rectangle& rectangle : metal_rectangles) {
    svg << "    <rect x=\"" << xl(rectangle) << "\" y=\"" << yl(rectangle)
        << "\" width=\"" << xh(rectangle) - xl(rectangle) << "\" height=\""
        << yh(rectangle) - yl(rectangle) << "\"/>\n";
  }
  svg << "  </g>\n";
  svg << std::fixed << std::setprecision(3);
  for (size_t index = 0; index < grid.tiles().size(); index++) {
    const Rect& tile = grid.tiles()[index];
    const double density = grid.metalAreas()[index] / tile.area();
    const int red = static_cast<int>(255.0 * (1.0 - density));
    const int green = static_cast<int>(255.0 * density);
    svg << "  <rect x=\"" << tile.xMin() << "\" y=\"" << tile.yMin()
        << "\" width=\"" << tile.dx() << "\" height=\"" << tile.dy()
        << "\" fill=\"rgb(" << red << ',' << green
        << ",0)\" fill-opacity=\"0.35\" stroke=\"#4e79a7\" stroke-width=\""
        << stroke_width << "\"/>\n";
  }
  svg << "  <g fill=\"none\" stroke=\"#e15759\" stroke-width=\""
      << stroke_width * 2 << "\">\n";
  for (const size_t window_index : analysis.window_indices) {
    if (window_index >= grid.windows().size()) {
      return false;
    }
    const Rect& window = grid.windows()[window_index].bounds;
    svg << "    <rect x=\"" << window.xMin() << "\" y=\"" << window.yMin()
        << "\" width=\"" << window.dx() << "\" height=\"" << window.dy()
        << "\"/>\n";
  }
  svg << "  </g>\n";
  svg << "  <text x=\"" << (bounds.xMin() + bounds.xMax()) / 2 << "\" y=\""
      << bounds.yMin() + std::max(1, height / 25)
      << "\" font-family=\"sans-serif\" font-size=\""
      << std::max(1, std::min(width, height) / 30)
      << "\" text-anchor=\"middle\">ML density: levels=" << analysis.levels
      << ", recorded windows=" << analysis.window_indices.size() << "</text>\n";
  svg << "</svg>\n";
  return svg.good();
}

MinVarFill::MinVarFill(dbDatabase* db, utl::Logger* logger, bool debug)
    : db_(db), logger_(logger)
{
  if (debug && Graphics::guiActive()) {
    graphics_ = std::make_unique<Graphics>();
  }
}

MinVarFill::~MinVarFill() = default;

void MinVarFill::fill(const char* rules_filename, const Rect& fill_area)
{
  layers_ = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  for (auto* layer : db_->getTech()->getLayers()) {
    const auto config = layers_.find(layer);
    if (config == layers_.end()) {
      logger_->warn(
          FIN, 23, "MinVar skipping layer {}.", layer->getConstName());
      continue;
    }
    fillLayer(block, layer, fill_area);
  }
}

void MinVarFill::benchmarkRectangleExtraction(const char* rules_filename,
                                              const Rect& fill_bounds_rect,
                                              int left_copies,
                                              int right_copies,
                                              int bottom_copies,
                                              int top_copies,
                                              int runs)
{
  const auto layers
      = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  const int tile_width = fill_bounds_rect.dx();
  const int tile_height = fill_bounds_rect.dy();
  const Rect scaled_bounds(
      fill_bounds_rect.xMin() - left_copies * tile_width,
      fill_bounds_rect.yMin() - bottom_copies * tile_height,
      fill_bounds_rect.xMax() + right_copies * tile_width,
      fill_bounds_rect.yMax() + top_copies * tile_height);
  const int horizontal_copies = left_copies + right_copies + 1;
  const int vertical_copies = bottom_copies + top_copies + 1;
  std::vector<Polygon90Set> fill_areas;
  for (auto* layer : db_->getTech()->getLayers()) {
    const auto config = layers.find(layer);
    if (config == layers.end()) {
      continue;
    }
    const Polygon90Set source_non_fill = orNonFills(block, layer);
    std::vector<Rectangle> source_rectangles;
    get_rectangles(source_rectangles, source_non_fill);
    Polygon90Set non_fill;
    for (int row = -bottom_copies; row <= top_copies; row++) {
      for (int column = -left_copies; column <= right_copies; column++) {
        for (const auto& rectangle : source_rectangles) {
          non_fill.insert(makeRect(xl(rectangle) + column * tile_width,
                                   yl(rectangle) + row * tile_height,
                                   xh(rectangle) + column * tile_width,
                                   yh(rectangle) + row * tile_height));
        }
      }
    }
    Polygon90 fill_bounds = makeRect(scaled_bounds.xMin(),
                                     scaled_bounds.yMin(),
                                     scaled_bounds.xMax(),
                                     scaled_bounds.yMax());
    fill_areas.push_back(
        fill_bounds - (non_fill + config->second.non_opc.space_to_non_fill));
  }
  double total_seconds = 0.0;
  size_t rectangle_count = 0;
  for (int run = 0; run < runs; run++) {
    utl::Timer timer;
    rectangle_count = 0;
    for (const auto& fill_area : fill_areas) {
      std::vector<Rectangle> rectangles;
      get_rectangles(rectangles, fill_area);
      rectangle_count += rectangles.size();
    }
    total_seconds += timer.elapsed();
  }
  logger_->info(FIN,
                12,
                "MinVar fill-area extraction: layers={}, copies={} ({}x{}), "
                "runs={}, rectangles={}, average_seconds={:.6f}.",
                fill_areas.size(),
                horizontal_copies * vertical_copies,
                horizontal_copies,
                vertical_copies,
                runs,
                rectangle_count,
                total_seconds / runs);
}

double MinVarFill::tileGridMetalArea(
    const char* rules_filename,
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
  struct LayerDensityReport
  {
    std::string name;
    size_t window_count;
    double min_density;
    double max_density;
    double mean_density;
    double variance;
    size_t min_violation_window_count;
    size_t max_violation_window_count;
    WindowDensityHistogram histogram;
    double compute_seconds;
    double svg_seconds;
  };
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
  std::vector<LayerDensityReport> density_reports;
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
      std::ofstream profile(profile_filename);
      if (!profile) {
        logger_->warn(FIN,
                      65,
                      "Cannot write floating-density profile to {}.",
                      profile_filename);
      } else {
        profile << std::fixed << std::setprecision(9);
        profile << "{\n  \"layer\": \"" << layer->getConstName()
                << "\",\n  \"algorithm\": \"" << algorithm
                << "\",\n  \"min_density\": " << floating_density.min_density
                << ",\n  \"max_density\": " << floating_density.max_density
                << ",\n  \"min_density_limit\": " << min_window_density;
        if (max_density >= 0.0) {
          profile << ",\n  \"max_density_limit\": " << max_density;
        }
        profile << ",\n  \"profiles\": [\n";
        for (size_t profile_index = 0;
             profile_index < floating_density.profiles.size();
             profile_index++) {
          const FloatingDensityResult::Profile& entry
              = floating_density.profiles[profile_index];
          profile << "    {\"label\": \"window y=" << entry.y
                  << "\", \"y\": " << entry.y << ", \"points\": [";
          for (size_t point_index = 0; point_index < entry.points.size();
               point_index++) {
            const auto& point = entry.points[point_index];
            profile << "[" << point.first << ", " << point.second << "]"
                    << (point_index + 1 == entry.points.size() ? "" : ", ");
          }
          profile << "]}"
                  << (profile_index + 1 == floating_density.profiles.size()
                          ? "\n"
                          : ",\n");
        }
        profile << "  ]\n}\n";
        logger_->info(FIN,
                      66,
                      "Floating density: layer={}, min={:.6f}, max={:.6f}, "
                      "profile={}",
                      layer->getConstName(),
                      floating_density.min_density,
                      floating_density.max_density,
                      profile_filename);
      }
    }
    total_area += layer_area;
    if (svg_filename != nullptr && svg_filename[0] != '\0') {
      compute_seconds += seconds(compute_slice_start, Clock::now());
      const auto svg_start = Clock::now();
      grid.writeSvg(
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
  if (density_report_filename != nullptr
      && density_report_filename[0] != '\0') {
    std::ofstream report(density_report_filename);
    if (!report) {
      logger_->warn(FIN,
                    59,
                    "Cannot write tile-grid density report to {}.",
                    density_report_filename);
    } else {
      report << std::fixed << std::setprecision(6);
      report << "{\n  \"runtime_seconds\": {\n"
             << "    \"density_analysis_excluding_svg\": " << compute_seconds
             << ",\n"
             << "    \"svg\": " << svg_seconds << "\n  },\n"
             << "  \"min_window_density_limit\": " << min_window_density
             << ",\n"
             << "  \"max_density_limit\": " << max_density << ",\n"
             << "  \"layers\": [\n";
      for (size_t index = 0; index < density_reports.size(); index++) {
        const LayerDensityReport& entry = density_reports[index];
        report << "    {\"layer\": \"" << entry.name
               << "\", \"window_count\": " << entry.window_count
               << ", \"min_density\": " << entry.min_density
               << ", \"max_density\": " << entry.max_density
               << ", \"mean_density\": " << entry.mean_density
               << ", \"variance\": " << entry.variance
               << ", \"min_violation_window_count\": "
               << entry.min_violation_window_count
               << ", \"max_violation_window_count\": "
               << entry.max_violation_window_count
               << ", \"runtime_seconds\": {\"compute\": "
               << entry.compute_seconds << ", \"svg\": " << entry.svg_seconds
               << "}";
        writeWindowDensityHistogram(report, entry.histogram);
        report << "}" << (index + 1 == density_reports.size() ? "\n" : ",\n");
      }
      report << "  ]\n}\n";
    }
  }
  return total_area;
}

double MinVarFill::fixedDissectionLp(const char* rules_filename,
                                     const Rect& region,
                                     const odb::Point& origin,
                                     int window_size,
                                     int resolution,
                                     double max_density,
                                     const char* svg_filename)
{
  const auto layers
      = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  TileGrid grid({region, origin, window_size, resolution});
  double total_fill_area = 0.0;
  for (auto* layer : db_->getTech()->getLayers()) {
    if (layers.find(layer) == layers.end()) {
      continue;
    }

    const Polygon90Set layer_shapes = orNonFills(block, layer);
    grid.calculateMetalDensities(layer_shapes);
    const auto [min_window_density, max_window_density]
        = getWindowDensityRange(grid.windows());
    FixedDissectionLpProblem problem;
    problem.max_window_density = max_density;
    problem.feature_areas = grid.metalAreas();
    problem.windows = grid.windowTileIndices();
    problem.tile_areas.reserve(grid.tiles().size());
    problem.max_fill_areas.reserve(grid.tiles().size());
    for (size_t index = 0; index < grid.tiles().size(); index++) {
      const double tile_area = grid.tiles()[index].area();
      problem.tile_areas.push_back(tile_area);
      // This is an ideal area capacity for LP planning.  Physical placement
      // will replace it with spacing- and pattern-aware tile slack.
      problem.max_fill_areas.push_back(
          std::max(tile_area - problem.feature_areas[index], 0.0));
    }

    const FixedDissectionLpResult result = solveFixedDissectionLp(problem);
    if (!result.solved) {
      logger_->error(FIN,
                     30,
                     "Fixed-dissection LP failed for layer {}.",
                     layer->getConstName());
    }
    for (size_t window_index = 0; window_index < grid.windows().size();
         window_index++) {
      DensityWindow& window = grid.windows()[window_index];
      for (const size_t tile_index : problem.windows[window_index]) {
        window.planned_fill_area += result.fill_areas[tile_index];
      }
      window.post_fill_density = (window.metal_area + window.planned_fill_area)
                                 / window.bounds.area();
    }
    const auto [min_post_fill_density, max_post_fill_density]
        = getWindowPostFillDensityRange(grid.windows());
    const double planned_fill_area = std::accumulate(
        result.fill_areas.begin(), result.fill_areas.end(), 0.0);
    logger_->info(FIN,
                  31,
                  "Fixed-dissection LP: layer={}, tiles={}, windows={}, "
                  "planned_fill_area={:.0f} DBU^2, "
                  "min_window_density={:.6f}, max_window_density={:.6f}, "
                  "min_post_fill_density={:.6f}, "
                  "max_post_fill_density={:.6f}.",
                  layer->getConstName(),
                  grid.tiles().size(),
                  grid.windows().size(),
                  planned_fill_area,
                  min_window_density,
                  max_window_density,
                  min_post_fill_density,
                  max_post_fill_density);
    if (svg_filename != nullptr && svg_filename[0] != '\0') {
      grid.writeSvg(
          std::string(svg_filename) + "_" + layer->getConstName() + ".svg",
          layer_shapes,
          db_->getTech()->getDbUnitsPerMicron(),
          nullptr,
          false);
      grid.writeLpDensityMaps(
          std::string(svg_filename) + "_" + layer->getConstName(),
          layer_shapes,
          result.fill_areas);
    }
    total_fill_area += planned_fill_area;
  }
  return total_fill_area;
}

double MinVarFill::fixedDissectionLpFill(const char* rules_filename,
                                         const Rect& region,
                                         const odb::Point& origin,
                                         int window_size,
                                         int resolution,
                                         double min_tile_density,
                                         double max_tile_density,
                                         double min_window_density,
                                         double max_window_density,
                                         const char* svg_filename,
                                         const char* density_report_filename)
{
  return fixedDissectionLpFillImpl(
      rules_filename,
      region,
      origin,
      window_size,
      resolution,
      min_tile_density,
      max_tile_density,
      min_window_density,
      max_window_density,
      svg_filename,
      density_report_filename,
      FixedDissectionLpObjective::kMaximizeMinimumWindowArea,
      0);
}

double MinVarFill::fixedDissectionLpMinAmountFill(
    const char* rules_filename,
    const Rect& region,
    const odb::Point& origin,
    int window_size,
    int resolution,
    double min_tile_density,
    double max_tile_density,
    double min_window_density,
    double max_window_density,
    const char* svg_filename,
    const char* density_report_filename)
{
  return fixedDissectionLpFillImpl(
      rules_filename,
      region,
      origin,
      window_size,
      resolution,
      min_tile_density,
      max_tile_density,
      min_window_density,
      max_window_density,
      svg_filename,
      density_report_filename,
      FixedDissectionLpObjective::kMinimizeTotalFillArea,
      0);
}

double MinVarFill::fixedDissectionLpLipFill(const char* rules_filename,
                                            const Rect& region,
                                            const odb::Point& origin,
                                            int window_size,
                                            int resolution,
                                            double min_tile_density,
                                            double max_tile_density,
                                            double min_window_density,
                                            double max_window_density,
                                            int lip_type,
                                            const char* svg_filename,
                                            const char* density_report_filename)
{
  return fixedDissectionLpFillImpl(
      rules_filename,
      region,
      origin,
      window_size,
      resolution,
      min_tile_density,
      max_tile_density,
      min_window_density,
      max_window_density,
      svg_filename,
      density_report_filename,
      FixedDissectionLpObjective::kMaximizeMinimumWindowArea,
      lip_type);
}

double MinVarFill::fixedDissectionLpFillImpl(
    const char* rules_filename,
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
    FixedDissectionLpObjective objective,
    int lip_type)
{
  struct LayerDensityReport
  {
    std::string name;
    double planned_fill_area;
    double placed_fill_area;
    size_t window_count;
    double min_density;
    double max_density;
    double mean_density;
    double variance;
    size_t min_violation_window_count;
    size_t max_violation_window_count;
    WindowDensityHistogram histogram;
    bool solved;
    std::string infeasibility_reason;
    size_t diagnostic_window_index;
    Rect diagnostic_window_bounds;
    double diagnostic_current_density;
    double diagnostic_required_fill_area;
    double diagnostic_max_legal_fill_area;
    double diagnostic_fill_budget;
    size_t tile_violation_count;
    size_t window_violation_count;
    double candidate_seconds;
    double solve_seconds;
    double placement_seconds;
    double compute_seconds;
    double svg_seconds;
  };
  using Clock = std::chrono::steady_clock;
  const auto seconds
      = [](const Clock::time_point& start, const Clock::time_point& end) {
          return std::chrono::duration<double>(end - start).count();
        };
  const auto compute_start = Clock::now();
  auto compute_slice_start = compute_start;
  double compute_seconds = 0.0;
  double svg_seconds = 0.0;
  std::vector<LayerDensityReport> density_reports;
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
            = objective == FixedDissectionLpObjective::kMinimizeTotalFillArea
                  ? std::min(min_window_density + 5e-4, max_window_density)
                  : min_window_density;
        problem.feature_areas = grid.metalAreas();
        problem.windows = grid.windowTileIndices();
        problem.tile_areas.reserve(grid.tiles().size());
        problem.max_fill_areas.reserve(grid.tiles().size());
        for (const Rect& tile : grid.tiles()) {
          Polygon90Set tile_fillable_area;
          std::vector<Rectangle> candidates
              = makeTileFillCandidates(tile,
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
                  ? solveFixedDissectionLp(problem, objective)
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
          grid.writeSvg(layer_svg + "_fillable.svg",
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
          grid.writeSvg(layer_svg + ".svg",
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
          grid.writeSvg(
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
          grid.writeSvg(
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
    if (density_report_filename != nullptr
        && density_report_filename[0] != '\0') {
      std::ofstream report(density_report_filename);
      if (!report) {
        logger_->warn(FIN,
                      57,
                      "Cannot write fixed-dissection LP density report to {}.",
                      density_report_filename);
      } else {
        report << std::fixed << std::setprecision(6);
        report << "{\n  \"runtime_seconds\": {\n"
               << "    \"compute_excluding_svg\": " << compute_seconds << ",\n"
               << "    \"svg\": " << svg_seconds << "\n  },\n"
               << "  \"min_window_density_limit\": " << min_window_density
               << ",\n"
               << "  \"max_window_density_limit\": " << max_window_density
               << ",\n"
               << "  \"layers\": [\n";
        for (size_t index = 0; index < density_reports.size(); index++) {
          const LayerDensityReport& entry = density_reports[index];
          report << "    {\"layer\": \"" << entry.name
                 << "\", \"planned_fill_area\": " << entry.planned_fill_area
                 << ", \"placed_fill_area\": " << entry.placed_fill_area
                 << ", \"window_count\": " << entry.window_count
                 << ", \"min_density\": " << entry.min_density
                 << ", \"max_density\": " << entry.max_density
                 << ", \"mean_density\": " << entry.mean_density
                 << ", \"variance\": " << entry.variance
                 << ", \"min_violation_window_count\": "
                 << entry.min_violation_window_count
                 << ", \"max_violation_window_count\": "
                 << entry.max_violation_window_count;
          writeWindowDensityHistogram(report, entry.histogram);
          report << ", \"solved\": " << (entry.solved ? "true" : "false")
                 << ", \"status\": \""
                 << (entry.solved ? "solved" : "infeasible") << "\""
                 << ", \"tile_violation_count\": " << entry.tile_violation_count
                 << ", \"window_violation_count\": "
                 << entry.window_violation_count << ", \"failure\": ";
          if (entry.solved) {
            report << "null";
          } else {
            report << "{\"reason\": \"" << entry.infeasibility_reason
                   << "\", \"window_index\": " << entry.diagnostic_window_index
                   << ", \"bounds\": [" << entry.diagnostic_window_bounds.xMin()
                   << ", " << entry.diagnostic_window_bounds.yMin() << ", "
                   << entry.diagnostic_window_bounds.xMax() << ", "
                   << entry.diagnostic_window_bounds.yMax()
                   << "], \"current_density\": "
                   << entry.diagnostic_current_density
                   << ", \"required_fill_area\": "
                   << entry.diagnostic_required_fill_area
                   << ", \"max_legal_fill_area\": "
                   << entry.diagnostic_max_legal_fill_area
                   << ", \"max_window_fill_budget\": "
                   << entry.diagnostic_fill_budget << "}";
          }
          report << ", \"runtime_seconds\": {\"candidates\": "
                 << entry.candidate_seconds
                 << ", \"lp_solve\": " << entry.solve_seconds
                 << ", \"placement\": " << entry.placement_seconds
                 << ", \"compute\": " << entry.compute_seconds
                 << ", \"svg\": " << entry.svg_seconds << "}}"
                 << (index + 1 == density_reports.size() ? "\n" : ",\n");
        }
        report << "  ]\n}\n";
      }
    }
    return total_placed_area;
  } catch (...) {
    for (dbFill* fill : created_fills) {
      dbFill::destroy(fill);
    }
    throw;
  }
}

double MinVarFill::multilevelFixedDissectionLp(const char* rules_filename,
                                               const Rect& region,
                                               const odb::Point& origin,
                                               int window_size,
                                               int resolution,
                                               double relative_accuracy,
                                               double max_density,
                                               const char* svg_filename)
{
  const auto layers
      = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  TileGrid grid({region, origin, window_size, resolution});
  double total_fill_area = 0.0;
  for (auto* layer : db_->getTech()->getLayers()) {
    if (layers.find(layer) == layers.end()) {
      continue;
    }
    const Polygon90Set layer_shapes = orNonFills(block, layer);
    grid.calculateMetalDensities(layer_shapes);
    const MultilevelDensityAnalysisResult analysis
        = grid.analyzeMultilevelDensity(relative_accuracy);
    if (svg_filename != nullptr && svg_filename[0] != '\0') {
      writeMLTileDensitySvg(
          std::string(svg_filename) + "_" + layer->getConstName() + ".svg",
          grid,
          analysis,
          layer_shapes,
          region);
    }
    if (analysis.window_indices.empty()) {
      logger_->warn(
          FIN,
          36,
          "Multilevel analysis found no complete windows on layer {}.",
          layer->getConstName());
      continue;
    }
    MLFixedDissectionLpProblem problem;
    problem.max_density = max_density;
    problem.feature_areas = grid.metalAreas();
    problem.windows = grid.windowTileIndices(analysis.window_indices);
    for (size_t index = 0; index < grid.tiles().size(); index++) {
      const double tile_area = grid.tiles()[index].area();
      problem.tile_areas.push_back(tile_area);
      problem.max_fill_areas.push_back(
          std::max(tile_area - problem.feature_areas[index], 0.0));
    }
    const MLFixedDissectionLpResult result = solveMLFixedDissectionLp(problem);
    if (!result.solved) {
      logger_->error(FIN,
                     37,
                     "Multilevel fixed-dissection LP failed for layer {}.",
                     layer->getConstName());
    }
    const double planned_fill_area = std::accumulate(
        result.fill_areas.begin(), result.fill_areas.end(), 0.0);
    logger_->info(FIN,
                  38,
                  "Multilevel fixed-dissection LP: layer={}, levels={}, "
                  "recorded_windows={}, planned_fill_area={:.0f} DBU^2, "
                  "max_window_area={:.0f}, bloat_max_window_area={:.0f}.",
                  layer->getConstName(),
                  analysis.levels,
                  analysis.window_indices.size(),
                  planned_fill_area,
                  analysis.max_window_area,
                  analysis.bloat_max_window_area);
    total_fill_area += planned_fill_area;
  }
  return total_fill_area;
}

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "FillCommon.h"
#include "boost/json.hpp"

namespace fin {
namespace {
std::vector<int> makeEdges(const int low,
                           const int high,
                           const int size,
                           const int step)
{
  if (low >= high || size <= 0 || step <= 0 || int64_t{low} + size > high) {
    throw std::invalid_argument(
        "Positive density windows and steps must fit inside the core.");
  }
  if (size % step != 0) {
    throw std::invalid_argument(
        "Density window dimensions must be divisible by the tile size.");
  }
  std::vector<int> edges;
  for (int64_t edge = low; edge < high; edge += step) {
    edges.push_back(edge);
  }
  edges.push_back(high);
  return edges;
}

size_t edgeIndex(const std::vector<int>& edges, const int coordinate)
{
  return std::lower_bound(edges.begin(), edges.end(), coordinate)
         - edges.begin();
}
}

FillGrid::FillGrid(const odb::Rect& region,
                   const odb::Point& window_size,
                   const odb::Point& window_step)
    : x_edges_(makeEdges(region.xMin(),
                         region.xMax(),
                         window_size.x(),
                         window_step.x())),
      y_edges_(makeEdges(region.yMin(),
                         region.yMax(),
                         window_size.y(),
                         window_step.y()))
{
  const size_t columns = x_edges_.size() - 1;
  const size_t rows = y_edges_.size() - 1;
  if (columns > std::numeric_limits<int>::max() / rows) {
    throw std::invalid_argument(
        "Density grid exceeds the LP solver's index range.");
  }
  tiles_.reserve(columns * rows);
  for (size_t row = 0; row < rows; ++row) {
    for (size_t col = 0; col < columns; ++col) {
      tiles_.emplace_back(
          x_edges_[col], y_edges_[row], x_edges_[col + 1], y_edges_[row + 1]);
    }
  }
  for (int64_t y = region.yMin(); y + window_size.y() <= region.yMax();
       y += window_step.y()) {
    for (int64_t x = region.xMin(); x + window_size.x() <= region.xMax();
         x += window_step.x()) {
      FillWindow window{
          odb::Rect(x, y, x + window_size.x(), y + window_size.y()), {}};
      const size_t left = edgeIndex(x_edges_, x);
      const size_t right = edgeIndex(x_edges_, x + window_size.x());
      const size_t bottom = edgeIndex(y_edges_, y);
      const size_t top = edgeIndex(y_edges_, y + window_size.y());
      for (size_t row = bottom; row < top; ++row) {
        for (size_t col = left; col < right; ++col) {
          window.tiles.push_back(row * columns + col);
        }
      }
      windows_.push_back(std::move(window));
    }
  }
}

const std::vector<odb::Rect>& FillGrid::tiles() const
{
  return tiles_;
}

const std::vector<FillWindow>& FillGrid::windows() const
{
  return windows_;
}

std::vector<Polygon90Set> FillGrid::partition(
    const std::vector<Rectangle>& rectangles) const
{
  const size_t columns = x_edges_.size() - 1;
  std::vector<Polygon90Set> result(tiles_.size());
  for (const auto& rectangle : rectangles) {
    const int left = std::max(xl(rectangle), x_edges_.front());
    const int bottom = std::max(yl(rectangle), y_edges_.front());
    const int right = std::min(xh(rectangle), x_edges_.back());
    const int top = std::min(yh(rectangle), y_edges_.back());
    if (left >= right || bottom >= top) {
      continue;
    }
    const size_t first_x
        = std::upper_bound(x_edges_.begin(), x_edges_.end(), left)
          - x_edges_.begin() - 1;
    const size_t last_x = edgeIndex(x_edges_, right);
    const size_t first_y
        = std::upper_bound(y_edges_.begin(), y_edges_.end(), bottom)
          - y_edges_.begin() - 1;
    const size_t last_y = edgeIndex(y_edges_, top);
    for (size_t row = first_y; row < last_y; ++row) {
      for (size_t col = first_x; col < last_x; ++col) {
        result[row * columns + col].insert(
            Rectangle(std::max(left, x_edges_[col]),
                      std::max(bottom, y_edges_[row]),
                      std::min(right, x_edges_[col + 1]),
                      std::min(top, y_edges_[row + 1])));
      }
    }
  }
  return result;
}

std::vector<double> FillGrid::metalAreas(
    const std::vector<Rectangle>& rectangles,
    const int num_threads) const
{
  auto parts = partition(rectangles);
  std::vector<double> areas(parts.size());
  forEachFillTile(parts.size(), num_threads, [&](const size_t i) {
    areas[i] = boost::polygon::area(parts[i]);
  });
  return areas;
}

std::vector<double> FillGrid::windowDensities(
    const std::vector<double>& tile_metal_areas) const
{
  if (tile_metal_areas.size() != tiles_.size()) {
    throw std::invalid_argument(
        "Density tile area count does not match the grid.");
  }
  std::vector<double> result;
  result.reserve(windows_.size());
  for (const auto& window : windows_) {
    double metal = 0.0;
    for (const size_t tile : window.tiles) {
      metal += tile_metal_areas[tile];
    }
    result.push_back(metal / static_cast<double>(window.bounds.area()));
  }
  return result;
}

FillDensitySummary summarizeFillDensity(const std::vector<double>& densities,
                                        const double minimum,
                                        const double maximum)
{
  FillDensitySummary summary;
  if (densities.empty()) {
    return summary;
  }
  summary.minimum = *std::ranges::min_element(densities);
  summary.maximum = *std::ranges::max_element(densities);
  summary.mean = std::accumulate(densities.begin(), densities.end(), 0.0)
                 / densities.size();
  for (const double density : densities) {
    summary.below_minimum += density < minimum;
    summary.above_maximum += density > maximum;
  }
  return summary;
}
namespace {
void checkWindowPositions(const odb::Rect& area,
                          const DensityGridConfig& config,
                          const double requested_window_size,
                          const int dbu,
                          const char* command,
                          const char* area_name,
                          utl::Logger* logger)
{
  const int64_t width = int64_t{area.xMax()} - area.xMin();
  const int64_t height = int64_t{area.yMax()} - area.yMin();
  const auto count = [&](const int64_t length) -> int64_t {
    return length < config.window_size
               ? 0
               : (length - config.window_size) / config.tile_size + 1;
  };
  const int64_t positions_x = count(width);
  const int64_t positions_y = count(height);
  if (positions_x < 2 || positions_y < 2) {
    const double width_um = static_cast<double>(width) / dbu;
    const double height_um = static_cast<double>(height) / dbu;
    const double window_um = static_cast<double>(config.window_size) / dbu;
    logger->error(utl::FIN,
                  24,
                  "{} requires at least two window positions in both X "
                  "and Y. Requested window size: {:.12g} um; effective window "
                  "size: {:.12g} x {:.12g} um; {} size: {:.12g} x {:.12g} um "
                  "(area {:.12g} um^2); window positions: X={}, Y={}. "
                  "Reduce -window_size.",
                  command,
                  requested_window_size,
                  window_um,
                  window_um,
                  area_name,
                  width_um,
                  height_um,
                  width_um * height_um,
                  positions_x,
                  positions_y);
  }
}
}

DensityGridConfig makeDensityGridConfig(odb::dbTech* tech,
                                        const odb::Rect& area,
                                        const double window_size,
                                        const int resolution,
                                        const char* command,
                                        const char* area_name,
                                        utl::Logger* logger)
{
  if (!std::isfinite(window_size) || window_size <= 0) {
    throw std::invalid_argument("Window size must be finite and positive.");
  }
  if (resolution < 1) {
    throw std::invalid_argument("Resolution must be a positive integer.");
  }
  if (area.xMin() >= area.xMax() || area.yMin() >= area.yMax()) {
    throw std::invalid_argument(
        "Density check area must have positive width and height.");
  }
  const int dbu = tech->getDbUnitsPerMicron();
  const double requested = std::round(window_size * dbu);
  if (!std::isfinite(requested) || requested < 1
      || requested > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(
        "Window size must be positive and representable in DBU.");
  }
  const int requested_dbu = static_cast<int>(requested);
  const int manufacturing_grid = std::max(tech->getManufacturingGrid(), 1);
  const int64_t quantum = int64_t{resolution} * manufacturing_grid;
  const int tile_size
      = static_cast<int>((requested_dbu / quantum) * manufacturing_grid);
  if (tile_size == 0) {
    throw std::invalid_argument(
        "Resolution is too high for the window size on the manufacturing grid; "
        "tiles must be at least one manufacturing grid unit wide.");
  }
  const DensityGridConfig config{
      tile_size * resolution, tile_size, manufacturing_grid};
  if (config.window_size != requested_dbu) {
    logger->warn(utl::FIN,
                 18,
                 "Adjusted window size from {:.12g} um ({} DBU) to {:.12g} "
                 "um ({} DBU) for resolution {} and manufacturing grid {} "
                 "DBU; tile size and window step are {:.12g} um ({} DBU).",
                 window_size,
                 requested_dbu,
                 static_cast<double>(config.window_size) / dbu,
                 config.window_size,
                 resolution,
                 manufacturing_grid,
                 static_cast<double>(tile_size) / dbu,
                 tile_size);
  }
  checkWindowPositions(
      area, config, window_size, dbu, command, area_name, logger);
  return config;
}

Polygon90Set collectLayerMetal(odb::dbBlock* block, odb::dbTechLayer* layer)
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
  return metal;
}

namespace {
std::vector<odb::dbTechLayer*> densityLayers(
    odb::dbTech* tech,
    const std::vector<std::string>& names,
    utl::Logger* logger)
{
  std::vector<odb::dbTechLayer*> layers;
  if (names.empty()) {
    for (auto* layer : tech->getLayers()) {
      if (layer->getType() == odb::dbTechLayerType::ROUTING) {
        layers.push_back(layer);
      }
    }
  } else {
    for (const auto& name : names) {
      auto* layer = tech->findLayer(name.c_str());
      if (layer == nullptr
          || layer->getType() != odb::dbTechLayerType::ROUTING) {
        logger->error(utl::FIN,
                      27,
                      "Layer '{}' must name an existing routing layer.",
                      name);
      }
      if (std::find(layers.begin(), layers.end(), layer) == layers.end()) {
        layers.push_back(layer);
      }
    }
  }
  if (layers.empty()) {
    throw std::invalid_argument(
        "No routing layers are available for density checking.");
  }
  return layers;
}

boost::json::array boundsInMicrons(const odb::Rect& bounds, const double dbu)
{
  return {bounds.xMin() / dbu,
          bounds.yMin() / dbu,
          bounds.xMax() / dbu,
          bounds.yMax() / dbu};
}

boost::json::object layerDensityReport(
    const char* name,
    const FillGrid& grid,
    const std::vector<double>& densities,
    const FillDensitySummary& summary,
    const DiscreteDensityCheckOptions& options,
    const double dbu)
{
  boost::json::array windows;
  windows.reserve(densities.size());
  for (size_t i = 0; i < densities.size(); ++i) {
    windows.push_back(boost::json::object{
        {"bounds_um", boundsInMicrons(grid.windows()[i].bounds, dbu)},
        {"density", densities[i]},
        {"below_minimum", densities[i] < options.min_density},
        {"above_maximum", densities[i] > options.max_density}});
  }
  return {{"name", name},
          {"window_count", densities.size()},
          {"min_density", summary.minimum},
          {"max_density", summary.maximum},
          {"mean_density", summary.mean},
          {"below_minimum", summary.below_minimum},
          {"above_maximum", summary.above_maximum},
          {"windows", std::move(windows)}};
}
}

void checkDiscreteDensity(odb::dbDatabase* db,
                          utl::Logger* logger,
                          const DiscreteDensityCheckOptions& options)
{
  if (options.num_threads < 1) {
    throw std::invalid_argument("Density check thread count must be positive.");
  }
  if (!std::isfinite(options.min_density) || !std::isfinite(options.max_density)
      || options.min_density < 0 || options.max_density > 1
      || options.min_density > options.max_density) {
    throw std::invalid_argument(
        "Density bounds must satisfy 0 <= min_density <= max_density <= 1.");
  }
  if (db->getTech() == nullptr || db->getChip() == nullptr
      || db->getChip()->getBlock() == nullptr) {
    logger->error(
        utl::FIN, 26, "discrete_density_check requires a loaded design.");
  }
  auto* block = db->getChip()->getBlock();
  const auto area = options.has_area ? options.area : block->getCoreArea();
  const auto config
      = makeDensityGridConfig(db->getTech(),
                              area,
                              options.window_size,
                              options.resolution,
                              "discrete_density_check",
                              options.has_area ? "check area" : "core",
                              logger);
  const FillGrid grid(area,
                      odb::Point(config.window_size, config.window_size),
                      odb::Point(config.tile_size, config.tile_size));
  const auto layers = densityLayers(db->getTech(), options.layers, logger);
  std::ofstream output;
  if (!options.report.empty()) {
    output.open(options.report);
    if (!output) {
      logger->error(
          utl::FIN, 29, "Cannot open density report '{}'.", options.report);
    }
  }
  const double dbu = db->getTech()->getDbUnitsPerMicron();
  boost::json::object report{{"schema_version", 1},
                             {"area_um", boundsInMicrons(area, dbu)},
                             {"requested_window_size_um", options.window_size},
                             {"window_size_um", config.window_size / dbu},
                             {"tile_size_um", config.tile_size / dbu},
                             {"resolution", options.resolution},
                             {"min_density_limit", options.min_density},
                             {"max_density_limit", options.max_density},
                             {"layers", boost::json::array{}}};
  for (auto* layer : layers) {
    auto metal = collectLayerMetal(block, layer);
    std::vector<Rectangle> rectangles;
    metal.get_rectangles(rectangles);
    const auto densities = grid.windowDensities(
        grid.metalAreas(rectangles, options.num_threads));
    const auto summary = summarizeFillDensity(
        densities, options.min_density, options.max_density);
    logger->info(
        utl::FIN,
        28,
        "Discrete density: layer={}, windows={}, min={:.6f}, max={:.6f}, "
        "mean={:.6f}, below_minimum={}, above_maximum={}.",
        layer->getConstName(),
        densities.size(),
        summary.minimum,
        summary.maximum,
        summary.mean,
        summary.below_minimum,
        summary.above_maximum);
    if (!options.report.empty()) {
      report.at("layers").as_array().push_back(layerDensityReport(
          layer->getConstName(), grid, densities, summary, options, dbu));
    }
  }
  if (!options.report.empty()) {
    output << boost::json::serialize(report) << '\n';
    output.close();
    if (!output) {
      logger->error(
          utl::FIN, 30, "Failed to write density report '{}'.", options.report);
    }
  }
}

}

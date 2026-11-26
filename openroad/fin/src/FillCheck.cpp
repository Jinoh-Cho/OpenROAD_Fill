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
#include <unordered_map>
#include <utility>
#include <vector>

#include "FillCommon.h"
#include "boost/json.hpp"

namespace fin {
namespace {
int densityWindowDbu(const double microns, const int dbu)
{
  const double value = std::round(microns * dbu);
  if (!std::isfinite(microns) || microns <= 0 || !std::isfinite(value)
      || value < 1 || value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(
        "Window size must be positive and representable in DBU.");
  }
  return static_cast<int>(value);
}

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
  const int requested_dbu = densityWindowDbu(window_size, dbu);
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
std::ofstream openDensityReport(const std::string& filename,
                                utl::Logger* logger)
{
  std::ofstream output;
  if (!filename.empty()) {
    output.open(filename);
    if (!output) {
      logger->error(utl::FIN, 29, "Cannot open density report '{}'.", filename);
    }
  }
  return output;
}

void closeDensityReport(std::ofstream& output,
                        const std::string& filename,
                        utl::Logger* logger)
{
  if (!filename.empty()) {
    output.close();
    if (!output) {
      logger->error(
          utl::FIN, 30, "Failed to write density report '{}'.", filename);
    }
  }
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
  auto output = openDensityReport(options.report, logger);
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
  }
  closeDensityReport(output, options.report, logger);
}

namespace {
struct ContinuousGeometry
{
  std::vector<Rectangle> rectangles;
  std::unordered_map<int64_t, std::vector<size_t>> rows;
  std::vector<int> y_events;
  int window_size;
  int min_x;
  int max_x;
  int min_y;
  int max_y;
};

void addContinuousEvent(std::vector<int>& events,
                        const int64_t coordinate,
                        const int minimum,
                        const int maximum)
{
  if (coordinate >= minimum && coordinate <= maximum) {
    events.push_back(static_cast<int>(coordinate));
  }
}

ContinuousGeometry makeContinuousGeometry(const Polygon90Set& metal,
                                          const odb::Rect& area,
                                          const int window_size)
{
  if (window_size <= 0 || int64_t{area.xMax()} - area.xMin() < window_size
      || int64_t{area.yMax()} - area.yMin() < window_size) {
    throw std::invalid_argument(
        "Continuous density window must fit inside the check area.");
  }
  ContinuousGeometry geometry{{},
                              {},
                              {},
                              window_size,
                              area.xMin(),
                              area.xMax() - window_size,
                              area.yMin(),
                              area.yMax() - window_size};
  Polygon90Set clipped = metal;
  Polygon90Set region;
  region.insert(Rectangle(area.xMin(), area.yMin(), area.xMax(), area.yMax()));
  clipped = clipped & region;
  clipped.get_rectangles(geometry.rectangles);
  geometry.y_events = {geometry.min_y, geometry.max_y};
  for (size_t i = 0; i < geometry.rectangles.size(); ++i) {
    const auto& rectangle = geometry.rectangles[i];
    const int64_t bottom = yl(rectangle);
    const int64_t top = yh(rectangle);
    for (const int64_t y :
         {bottom - window_size, top - window_size, bottom, top}) {
      addContinuousEvent(geometry.y_events, y, geometry.min_y, geometry.max_y);
    }
    const int64_t first = (bottom - geometry.min_y) / window_size;
    const int64_t last = (top - 1 - geometry.min_y) / window_size;
    for (int64_t row = first; row <= last; ++row) {
      geometry.rows[row].push_back(i);
    }
  }
  std::ranges::sort(geometry.y_events);
  geometry.y_events.erase(
      std::unique(geometry.y_events.begin(), geometry.y_events.end()),
      geometry.y_events.end());
  return geometry;
}

std::vector<size_t> continuousRowRectangles(const ContinuousGeometry& geometry,
                                            const int y,
                                            std::vector<size_t>& seen,
                                            const size_t generation)
{
  std::vector<size_t> indices;
  const int64_t first = (int64_t{y} - geometry.min_y) / geometry.window_size;
  const int64_t last = (int64_t{y} + geometry.window_size - 1 - geometry.min_y)
                       / geometry.window_size;
  for (int64_t row = first; row <= last; ++row) {
    const auto bucket = geometry.rows.find(row);
    if (bucket == geometry.rows.end()) {
      continue;
    }
    for (const size_t index : bucket->second) {
      if (seen[index] != generation) {
        seen[index] = generation;
        const auto& rectangle = geometry.rectangles[index];
        if (yl(rectangle) < int64_t{y} + geometry.window_size
            && y < yh(rectangle)) {
          indices.push_back(index);
        }
      }
    }
  }
  return indices;
}

struct ContinuousXEvent
{
  int x;
  int64_t slope_delta;
};

struct ContinuousRowSweep
{
  int64_t area = 0;
  int64_t slope = 0;
  std::vector<ContinuousXEvent> events;
};

ContinuousRowSweep makeContinuousRowSweep(const ContinuousGeometry& geometry,
                                          const int y,
                                          const std::vector<size_t>& indices)
{
  ContinuousRowSweep sweep;
  if (geometry.max_x > geometry.min_x) {
    sweep.events.push_back({geometry.max_x, 0});
  }
  const auto add_event = [&](const int64_t x, const int64_t delta) {
    if (x > geometry.min_x && x <= geometry.max_x) {
      sweep.events.push_back({static_cast<int>(x), delta});
    }
  };
  for (const size_t index : indices) {
    const auto& rectangle = geometry.rectangles[index];
    const int64_t height
        = std::min(int64_t{y} + geometry.window_size, int64_t{yh(rectangle)})
          - std::max(int64_t{y}, int64_t{yl(rectangle)});
    const int64_t left = xl(rectangle);
    const int64_t right = xh(rectangle);
    const int64_t overlap_x = std::max<int64_t>(
        0,
        std::min(int64_t{geometry.min_x} + geometry.window_size, right)
            - std::max(int64_t{geometry.min_x}, left));
    sweep.area += overlap_x * height;
    if (geometry.min_x >= left - geometry.window_size
        && geometry.min_x < right - geometry.window_size) {
      sweep.slope += height;
    }
    if (geometry.min_x >= left && geometry.min_x < right) {
      sweep.slope -= height;
    }
    add_event(left - geometry.window_size, height);
    add_event(right - geometry.window_size, -height);
    add_event(left, -height);
    add_event(right, height);
  }
  std::ranges::sort(sweep.events, {}, &ContinuousXEvent::x);
  return sweep;
}

void validateDensityLimits(const double minimum, const double maximum)
{
  if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0
      || maximum > 1 || minimum > maximum) {
    throw std::invalid_argument(
        "Density bounds must satisfy 0 <= min_density <= max_density <= 1.");
  }
}
}

ContinuousDensityResult analyzeContinuousDensity(
    const Polygon90Set& metal,
    const odb::Rect& area,
    const int window_size,
    const double min_density,
    const double max_density,
    const std::function<void(const ContinuousDensityViolation&)>& on_violation)
{
  validateDensityLimits(min_density, max_density);
  const auto geometry = makeContinuousGeometry(metal, area, window_size);
  const double window_area = static_cast<double>(window_size) * window_size;
  ContinuousDensityResult result;
  int64_t minimum_area = std::numeric_limits<int64_t>::max();
  int64_t maximum_area = -1;
  const auto record = [&](const int x, const int y, const int64_t metal_area) {
    const odb::Rect bounds(x, y, x + window_size, y + window_size);
    if (metal_area < minimum_area) {
      minimum_area = metal_area;
      result.min_window = bounds;
    }
    if (metal_area > maximum_area) {
      maximum_area = metal_area;
      result.max_window = bounds;
    }
    ++result.evaluated_event_windows;
    const double density = static_cast<double>(metal_area) / window_area;
    const bool below = density < min_density;
    const bool above = density > max_density;
    result.below_minimum += below;
    result.above_maximum += above;
    if ((below || above) && on_violation) {
      on_violation({bounds, density, below});
    }
  };
  std::vector<size_t> seen(geometry.rectangles.size(), 0);
  size_t generation = 0;
  for (const int y : geometry.y_events) {
    if (++generation == 0) {
      std::fill(seen.begin(), seen.end(), 0);
      ++generation;
    }
    const auto indices = continuousRowRectangles(geometry, y, seen, generation);
    auto sweep = makeContinuousRowSweep(geometry, y, indices);
    record(geometry.min_x, y, sweep.area);
    int previous_x = geometry.min_x;
    for (size_t i = 0; i < sweep.events.size();) {
      const int x = sweep.events[i].x;
      sweep.area += sweep.slope * (int64_t{x} - previous_x);
      record(x, y, sweep.area);
      do {
        sweep.slope += sweep.events[i++].slope_delta;
      } while (i < sweep.events.size() && sweep.events[i].x == x);
      previous_x = x;
    }
  }
  result.min_density = static_cast<double>(minimum_area) / window_area;
  result.max_density = static_cast<double>(maximum_area) / window_area;
  return result;
}

namespace {
boost::json::object continuousLayerSummary(
    const char* name,
    const ContinuousDensityResult& result,
    const double dbu)
{
  return {{"name", name},
          {"min_density", result.min_density},
          {"max_density", result.max_density},
          {"min_window_um", boundsInMicrons(result.min_window, dbu)},
          {"max_window_um", boundsInMicrons(result.max_window, dbu)},
          {"evaluated_event_windows", result.evaluated_event_windows},
          {"below_minimum", result.below_minimum},
          {"above_maximum", result.above_maximum}};
}

void checkContinuousLayer(odb::dbBlock* block,
                          odb::dbTechLayer* layer,
                          const odb::Rect& area,
                          const int window_size,
                          const ContinuousDensityCheckOptions& options,
                          std::ofstream& output,
                          const bool first_layer,
                          utl::Logger* logger)
{
  const double dbu = block->getTech()->getDbUnitsPerMicron();
  bool first_violation = true;
  std::function<void(const ContinuousDensityViolation&)> emit;
  if (output.is_open()) {
    output << (first_layer ? "" : ",") << "{\"violations\":[";
    emit = [&](const ContinuousDensityViolation& violation) {
      const boost::json::object entry{
          {"bounds_um", boundsInMicrons(violation.bounds, dbu)},
          {"density", violation.density},
          {"type",
           violation.below_minimum ? "below_minimum" : "above_maximum"}};
      output << (first_violation ? "" : ",") << boost::json::serialize(entry);
      first_violation = false;
    };
  }
  const auto result = analyzeContinuousDensity(collectLayerMetal(block, layer),
                                               area,
                                               window_size,
                                               options.min_density,
                                               options.max_density,
                                               emit);
  logger->info(
      utl::FIN,
      34,
      "Continuous density (ALG3): layer={}, min={:.6f} at "
      "[{:.12g}, {:.12g}, {:.12g}, {:.12g}] um, max={:.6f} at "
      "[{:.12g}, {:.12g}, {:.12g}, {:.12g}] um; "
      "below_minimum={}, above_maximum={}, evaluated_event_windows={}.",
      layer->getConstName(),
      result.min_density,
      result.min_window.xMin() / dbu,
      result.min_window.yMin() / dbu,
      result.min_window.xMax() / dbu,
      result.min_window.yMax() / dbu,
      result.max_density,
      result.max_window.xMin() / dbu,
      result.max_window.yMin() / dbu,
      result.max_window.xMax() / dbu,
      result.max_window.yMax() / dbu,
      result.below_minimum,
      result.above_maximum,
      result.evaluated_event_windows);
  if (output.is_open()) {
    const std::string summary = boost::json::serialize(
        continuousLayerSummary(layer->getConstName(), result, dbu));
    output << "]," << summary.substr(1);
  }
}
}

void checkContinuousDensity(odb::dbDatabase* db,
                            utl::Logger* logger,
                            const ContinuousDensityCheckOptions& options)
{
  validateDensityLimits(options.min_density, options.max_density);
  if (db->getTech() == nullptr || db->getChip() == nullptr
      || db->getChip()->getBlock() == nullptr) {
    logger->error(
        utl::FIN, 33, "continuous_density_check requires a loaded design.");
  }
  auto* block = db->getChip()->getBlock();
  const auto area = options.has_area ? options.area : block->getCoreArea();
  if (area.xMin() >= area.xMax() || area.yMin() >= area.yMax()) {
    throw std::invalid_argument(
        "Density check area must have positive width and height.");
  }
  const int dbu = db->getTech()->getDbUnitsPerMicron();
  const int window_size = densityWindowDbu(options.window_size, dbu);
  checkWindowPositions(area,
                       {window_size, 1, 1},
                       options.window_size,
                       dbu,
                       "continuous_density_check",
                       options.has_area ? "check area" : "core",
                       logger);
  const auto layers = densityLayers(db->getTech(), options.layers, logger);
  auto output = openDensityReport(options.report, logger);
  if (output.is_open()) {
    const boost::json::object header{
        {"schema_version", 1},
        {"algorithm", "ALG3"},
        {"area_um", boundsInMicrons(area, dbu)},
        {"requested_window_size_um", options.window_size},
        {"window_size_um", static_cast<double>(window_size) / dbu},
        {"min_density_limit", options.min_density},
        {"max_density_limit", options.max_density},
        {"violation_representation", "representative_edge_event_windows"},
        {"all_continuous_positions_listed", false}};
    auto text = boost::json::serialize(header);
    text.pop_back();
    output << text << ",\"layers\":[";
  }
  bool first_layer = true;
  for (auto* layer : layers) {
    checkContinuousLayer(
        block, layer, area, window_size, options, output, first_layer, logger);
    first_layer = false;
  }
  if (output.is_open()) {
    output << "]}\n";
  }
  closeDensityReport(output, options.report, logger);
}

}

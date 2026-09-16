#include "FillUtill.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <stdexcept>

#include "boost/polygon/polygon.hpp"

namespace fin {

namespace {

using Rectangle = boost::polygon::rectangle_data<int>;
using Polygon90Set = boost::polygon::polygon_90_set_data<int>;
using boost::polygon::operators::operator&;
using boost::polygon::operators::operator+=;

Rectangle makeRectangle(const odb::Rect& rect)
{
  return Rectangle(rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax());
}

int alignedStart(int coordinate, int origin, int tile_size)
{
  const int offset = coordinate - origin;
  if (offset >= 0) {
    return origin + offset / tile_size * tile_size;
  }
  return origin - (-(offset + 1) / tile_size + 1) * tile_size;
}

bool rectanglesIntersect(const odb::Rect& lhs, const odb::Rect& rhs)
{
  return lhs.xMin() < rhs.xMax() && rhs.xMin() < lhs.xMax()
         && lhs.yMin() < rhs.yMax() && rhs.yMin() < lhs.yMax();
}

struct DensityMapCell
{
  size_t column;
  size_t row;
  odb::Rect bounds;
  double metal_density;
  double lp_density;
};

void writeSvgPolygonPath(std::ofstream& svg, const Polygon90& polygon)
{
  const auto write_ring = [&svg](const auto& ring) {
    bool first = true;
    for (auto point_it = ring.begin(); point_it != ring.end(); point_it++) {
      const auto point = *point_it;
      svg << (first ? "M " : "L ") << point.x() << ' ' << point.y() << ' ';
      first = false;
    }
    if (!first) {
      svg << "Z ";
    }
  };
  write_ring(polygon);
  for (auto hole = polygon.begin_holes(); hole != polygon.end_holes(); hole++) {
    write_ring(*hole);
  }
}

bool writeDensityMap(const std::string& filename,
                     const std::string& title,
                     const std::vector<DensityMapCell>& cells,
                     size_t columns,
                     size_t rows,
                     const Polygon90Set& metal_shapes,
                     const odb::Rect& region,
                     bool draw_regions)
{
  constexpr int cell_width = 180;
  constexpr int cell_height = 100;
  constexpr int circuit_width = 600;
  constexpr int gutter = 30;
  constexpr int title_height = 60;
  std::ofstream svg(filename);
  if (!svg) {
    return false;
  }
  const int map_width = columns * cell_width;
  const int map_height = rows * cell_height;
  const int width = circuit_width + gutter + map_width;
  const int height = map_height + title_height;
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << width
      << ' ' << height << "\">\n";
  svg << "  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  svg << "  <text x=\"" << width / 2 << "\" y=\"35\" "
      << "font-family=\"sans-serif\" font-size=\"24\" "
      << "font-weight=\"bold\" text-anchor=\"middle\">" << title << "</text>\n";

  const double scale
      = std::min(circuit_width / static_cast<double>(region.dx()),
                 map_height / static_cast<double>(region.dy()));
  const double circuit_x_offset = (circuit_width - region.dx() * scale) / 2.0;
  const double circuit_y_offset
      = title_height + (map_height - region.dy() * scale) / 2.0;
  svg << "  <rect x=\"" << circuit_x_offset << "\" y=\"" << circuit_y_offset
      << "\" width=\"" << region.dx() * scale << "\" height=\""
      << region.dy() * scale << "\" fill=\"#f8f8f8\" stroke=\"#222\"/>\n";
  Polygon90Set region_polygon;
  region_polygon.insert(makeRectangle(region));
  std::vector<Rectangle> metal_rectangles;
  boost::polygon::get_rectangles(metal_rectangles,
                                 metal_shapes & region_polygon);
  svg << "  <g fill=\"#606060\" fill-opacity=\"0.7\" stroke=\"none\">\n";
  for (const Rectangle& metal : metal_rectangles) {
    const double x = circuit_x_offset
                     + (boost::polygon::xl(metal) - region.xMin()) * scale;
    const double y = circuit_y_offset
                     + (region.yMax() - boost::polygon::yh(metal)) * scale;
    const double metal_width
        = (boost::polygon::xh(metal) - boost::polygon::xl(metal)) * scale;
    const double metal_height
        = (boost::polygon::yh(metal) - boost::polygon::yl(metal)) * scale;
    svg << "    <rect x=\"" << x << "\" y=\"" << y << "\" width=\""
        << metal_width << "\" height=\"" << metal_height << "\"/>\n";
  }
  svg << "  </g>\n";

  for (const DensityMapCell& cell : cells) {
    const double density = std::clamp(cell.lp_density, 0.0, 1.0);
    const int red = static_cast<int>(255.0 * (1.0 - density));
    const int green = static_cast<int>(255.0 * density);
    const double x
        = circuit_x_offset + (cell.bounds.xMin() - region.xMin()) * scale;
    const double y
        = circuit_y_offset + (region.yMax() - cell.bounds.yMax()) * scale;
    const double region_width = cell.bounds.dx() * scale;
    const double region_height = cell.bounds.dy() * scale;
    if (draw_regions) {
      svg << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\""
          << region_width << "\" height=\"" << region_height << "\" fill=\"rgb("
          << red << ',' << green
          << ",0)\" fill-opacity=\"0.45\" stroke=\"#222\"/>\n";
    } else {
      svg << "  <circle cx=\"" << x << "\" cy=\"" << y + region_height
          << "\" r=\"4\" fill=\"rgb(" << red << ',' << green
          << ",0)\" stroke=\"#222\"/>\n";
    }
  }

  svg << std::fixed << std::setprecision(3);
  for (const DensityMapCell& cell : cells) {
    const int x = circuit_width + gutter + cell.column * cell_width;
    const int y = (rows - cell.row - 1) * cell_height + title_height;
    svg << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << cell_width
        << "\" height=\"" << cell_height
        << "\" fill=\"#f2f2f2\" stroke=\"#555\"/>\n";
    svg << "  <text x=\"" << x + cell_width / 2 << "\" y=\"" << y + 22
        << "\" font-family=\"monospace\" font-size=\"16\" "
        << "text-anchor=\"middle\">(" << cell.column << ',' << cell.row
        << ")</text>\n";
    svg << "  <text x=\"" << x + cell_width / 2 << "\" y=\"" << y + 52
        << "\" font-family=\"monospace\" font-size=\"18\" "
        << "text-anchor=\"middle\">metal=" << cell.metal_density << "</text>\n";
    svg << "  <text x=\"" << x + cell_width / 2 << "\" y=\"" << y + 82
        << "\" font-family=\"monospace\" font-size=\"18\" "
        << "text-anchor=\"middle\">lp=" << cell.lp_density << "</text>\n";
  }
  svg << "</svg>\n";
  return svg.good();
}

}  // namespace

std::pair<double, double> getWindowDensityRange(
    const std::vector<DensityWindow>& windows)
{
  if (windows.empty()) {
    return {0.0, 0.0};
  }

  double min_density = windows.front().density;
  double max_density = min_density;
  for (const DensityWindow& window : windows) {
    min_density = std::min(min_density, window.density);
    max_density = std::max(max_density, window.density);
  }
  return {min_density, max_density};
}

std::pair<double, double> getWindowPostFillDensityRange(
    const std::vector<DensityWindow>& windows)
{
  if (windows.empty()) {
    return {0.0, 0.0};
  }

  double min_density = windows.front().post_fill_density;
  double max_density = min_density;
  for (const DensityWindow& window : windows) {
    min_density = std::min(min_density, window.post_fill_density);
    max_density = std::max(max_density, window.post_fill_density);
  }
  return {min_density, max_density};
}

TileGrid::TileGrid(const TileGridConfig& config) : region_(config.region)
{
  if (config.resolution <= 0 || config.window_size < config.resolution
      || config.window_size % config.resolution != 0) {
    throw std::invalid_argument(
        "TileGrid requires window_size to be divisible by a positive "
        "resolution.");
  }

  const int tile_size = config.window_size / config.resolution;
  tiles_per_window_ = config.resolution;
  const int first_tile_y
      = alignedStart(config.region.yMin(), config.origin.y(), tile_size);
  const int first_tile_x
      = alignedStart(config.region.xMin(), config.origin.x(), tile_size);
  for (int y = first_tile_y; y < config.region.yMax(); y += tile_size) {
    for (int x = first_tile_x; x < config.region.xMax(); x += tile_size) {
      tiles_.emplace_back(std::max(x, config.region.xMin()),
                          std::max(y, config.region.yMin()),
                          std::min(x + tile_size, config.region.xMax()),
                          std::min(y + tile_size, config.region.yMax()));
    }
  }
  for (int x = first_tile_x; x < config.region.xMax(); x += tile_size) {
    tile_columns_++;
  }

  int first_window_y = first_tile_y;
  if (first_window_y < config.region.yMin()) {
    first_window_y += tile_size;
  }
  int first_window_x = first_tile_x;
  if (first_window_x < config.region.xMin()) {
    first_window_x += tile_size;
  }
  for (int y = first_window_y; y + config.window_size <= config.region.yMax();
       y += tile_size) {
    for (int x = first_window_x; x + config.window_size <= config.region.xMax();
         x += tile_size) {
      windows_.push_back(
          {odb::Rect(x, y, x + config.window_size, y + config.window_size),
           static_cast<size_t>((x - first_tile_x) / tile_size),
           static_cast<size_t>((y - first_tile_y) / tile_size)});
    }
  }
}

std::vector<std::vector<size_t>> TileGrid::windowTileIndices() const
{
  std::vector<size_t> indices(windows_.size());
  std::iota(indices.begin(), indices.end(), 0);
  return windowTileIndices(indices);
}

std::vector<std::vector<size_t>> TileGrid::windowTileIndices(
    const std::vector<size_t>& window_indices) const
{
  std::vector<std::vector<size_t>> window_tiles;
  window_tiles.reserve(window_indices.size());
  for (const size_t window_index : window_indices) {
    if (window_index >= windows_.size()) {
      throw std::invalid_argument("A multilevel window index is invalid.");
    }
    const DensityWindow& window = windows_[window_index];
    std::vector<size_t> tile_indices;
    tile_indices.reserve(tiles_per_window_ * tiles_per_window_);
    for (size_t row = window.first_tile_y;
         row < window.first_tile_y + tiles_per_window_;
         row++) {
      for (size_t column = window.first_tile_x;
           column < window.first_tile_x + tiles_per_window_;
           column++) {
        tile_indices.push_back(row * tile_columns_ + column);
      }
    }
    window_tiles.push_back(std::move(tile_indices));
  }
  return window_tiles;
}

MultilevelDensityAnalysisResult TileGrid::analyzeMultilevelDensity(
    const double relative_accuracy) const
{
  if (relative_accuracy <= 0.0 || relative_accuracy > 1.0) {
    throw std::invalid_argument(
        "Multilevel relative accuracy must be between zero and one.");
  }
  if (metal_areas_.size() != tiles_.size()) {
    throw std::invalid_argument(
        "Calculate tile metal densities before multilevel analysis.");
  }
  if ((tiles_per_window_ & (tiles_per_window_ - 1)) != 0) {
    throw std::invalid_argument(
        "Multilevel analysis requires a power-of-two tile resolution.");
  }

  MultilevelDensityAnalysisResult result;
  if (windows_.empty()) {
    return result;
  }

  std::vector<odb::Rect> surviving_bloated_windows;
  std::vector<bool> recorded(windows_.size(), false);
  for (int level_resolution = 1; level_resolution <= tiles_per_window_;
       level_resolution *= 2) {
    const size_t stride = tiles_per_window_ / level_resolution;
    double level_max = 0.0;
    double level_bloat_max = 0.0;
    std::vector<odb::Rect> next_survivors;
    std::vector<bool> level_candidates(windows_.size(), false);

    for (size_t index = 0; index < windows_.size(); index++) {
      const DensityWindow& window = windows_[index];
      if (window.first_tile_x % stride != 0
          || window.first_tile_y % stride != 0) {
        continue;
      }
      if (!surviving_bloated_windows.empty()) {
        bool is_candidate = false;
        for (const odb::Rect& candidate : surviving_bloated_windows) {
          if (rectanglesIntersect(window.bounds, candidate)) {
            is_candidate = true;
            break;
          }
        }
        if (!is_candidate) {
          continue;
        }
      }

      level_candidates[index] = true;
      recorded[index] = true;
      level_max = std::max(level_max, window.metal_area);
      const size_t last_x = window.first_tile_x + tiles_per_window_ + stride;
      const size_t last_y = window.first_tile_y + tiles_per_window_ + stride;
      if (last_x > tile_columns_ || last_y * tile_columns_ > tiles_.size()) {
        continue;
      }
      double bloat_area = 0.0;
      for (size_t row = window.first_tile_y; row < last_y; row++) {
        for (size_t column = window.first_tile_x; column < last_x; column++) {
          bloat_area += metal_areas_[row * tile_columns_ + column];
        }
      }
      level_bloat_max = std::max(level_bloat_max, bloat_area);
    }

    for (size_t index = 0; index < windows_.size(); index++) {
      const DensityWindow& window = windows_[index];
      if (!level_candidates[index] || window.first_tile_x % stride != 0
          || window.first_tile_y % stride != 0) {
        continue;
      }
      const size_t last_x = window.first_tile_x + tiles_per_window_ + stride;
      const size_t last_y = window.first_tile_y + tiles_per_window_ + stride;
      if (last_x > tile_columns_ || last_y * tile_columns_ > tiles_.size()) {
        continue;
      }
      double bloat_area = 0.0;
      for (size_t row = window.first_tile_y; row < last_y; row++) {
        for (size_t column = window.first_tile_x; column < last_x; column++) {
          bloat_area += metal_areas_[row * tile_columns_ + column];
        }
      }
      if (bloat_area > level_max) {
        const odb::Rect& last_tile
            = tiles_[(last_y - 1) * tile_columns_ + last_x - 1];
        next_survivors.emplace_back(window.bounds.xMin(),
                                    window.bounds.yMin(),
                                    last_tile.xMax(),
                                    last_tile.yMax());
      }
    }

    result.levels++;
    result.max_window_area = level_max;
    result.bloat_max_window_area = level_bloat_max;
    const double relative_gap = level_max == 0.0
                                    ? (level_bloat_max == 0.0 ? 0.0 : 1.0)
                                    : (level_bloat_max - level_max) / level_max;
    if (relative_gap <= relative_accuracy || next_survivors.empty()
        || level_resolution == tiles_per_window_) {
      break;
    }
    surviving_bloated_windows = std::move(next_survivors);
  }

  for (size_t index = 0; index < recorded.size(); index++) {
    if (recorded[index]) {
      result.window_indices.push_back(index);
    }
  }
  return result;
}

bool TileGrid::writeSvg(
    const std::string& filename,
    const boost::polygon::polygon_90_set_data<int>& metal_shapes,
    int dbu_per_micron,
    const std::vector<double>* planned_fill_areas,
    bool show_tile_values,
    const boost::polygon::polygon_90_set_data<int>* placed_fill_shapes,
    const std::vector<Polygon90>* fillable_polygons,
    const std::vector<double>* target_tile_densities,
    const std::vector<TileViolation>* tile_violations,
    const std::vector<WindowViolation>* window_violations,
    const std::vector<double>* bloated_non_fill_areas,
    const std::vector<double>* fillable_region_areas) const
{
  if (planned_fill_areas != nullptr
      && planned_fill_areas->size() != tiles_.size()) {
    return false;
  }
  if (target_tile_densities != nullptr
      && target_tile_densities->size() != tiles_.size()) {
    return false;
  }
  if (bloated_non_fill_areas != nullptr
      && bloated_non_fill_areas->size() != tiles_.size()) {
    return false;
  }
  if (fillable_region_areas != nullptr
      && fillable_region_areas->size() != tiles_.size()) {
    return false;
  }
  std::ofstream svg(filename);
  if (!svg) {
    return false;
  }

  odb::Rect display_bounds = region_;
  for (const auto& window : windows_) {
    display_bounds.merge(window.bounds);
  }
  const int width = display_bounds.dx();
  const int height = display_bounds.dy();
  const int stroke_width = std::max(1, std::min(width, height) / 500);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
      << display_bounds.xMin() << ' ' << display_bounds.yMin() << ' ' << width
      << ' ' << height << "\">\n";
  svg << "  <rect x=\"" << region_.xMin() << "\" y=\"" << region_.yMin()
      << "\" width=\"" << region_.dx() << "\" height=\"" << region_.dy()
      << "\" fill=\"white\" stroke=\"black\"/>\n";
  Polygon90Set region_polygon;
  region_polygon.insert(makeRectangle(region_));
  Polygon90Set clipped_metal = metal_shapes & region_polygon;
  std::vector<Rectangle> metal_rectangles;
  boost::polygon::get_rectangles(metal_rectangles, clipped_metal);
  svg << "  <g fill=\"#808080\" fill-opacity=\"0.55\" stroke=\"none\">\n";
  for (const auto& metal : metal_rectangles) {
    svg << "    <rect x=\"" << boost::polygon::xl(metal) << "\" y=\""
        << boost::polygon::yl(metal) << "\" width=\""
        << boost::polygon::xh(metal) - boost::polygon::xl(metal)
        << "\" height=\""
        << boost::polygon::yh(metal) - boost::polygon::yl(metal) << "\"/>\n";
  }
  svg << "  </g>\n";
  if (fillable_polygons != nullptr) {
    svg << "  <g fill=\"#43a047\" fill-opacity=\"0.35\" stroke=\"#1f1f1f\" "
           "stroke-width=\""
        << std::max(1, stroke_width / 2) << "\">\n";
    for (const Polygon90& polygon : *fillable_polygons) {
      svg << "    <path d=\"";
      writeSvgPolygonPath(svg, polygon);
      svg << "\" fill-rule=\"evenodd\"/>\n";
    }
    svg << "  </g>\n";
  }
  Polygon90Set displayed_metal = metal_shapes;
  if (placed_fill_shapes != nullptr) {
    Polygon90Set clipped_fill = *placed_fill_shapes & region_polygon;
    std::vector<Rectangle> fill_rectangles;
    boost::polygon::get_rectangles(fill_rectangles, clipped_fill);
    svg << "  <g fill=\"#1976d2\" fill-opacity=\"0.80\" stroke=\"none\">\n";
    for (const Rectangle& fill : fill_rectangles) {
      svg << "    <rect x=\"" << boost::polygon::xl(fill) << "\" y=\""
          << boost::polygon::yl(fill) << "\" width=\""
          << boost::polygon::xh(fill) - boost::polygon::xl(fill)
          << "\" height=\""
          << boost::polygon::yh(fill) - boost::polygon::yl(fill) << "\"/>\n";
    }
    svg << "  </g>\n";
    displayed_metal += *placed_fill_shapes;
  }
  if (tile_violations != nullptr) {
    svg << "  <g fill-opacity=\"0.50\" stroke-width=\"" << stroke_width
        << "\">\n";
    for (const TileViolation& violation : *tile_violations) {
      if (violation.tile_index >= tiles_.size()) {
        continue;
      }
      const odb::Rect& tile = tiles_[violation.tile_index];
      const char* color = violation.reason == TileViolationReason::kCapacity
                              ? "#e53935"
                              : "#fb8c00";
      svg << "    <rect x=\"" << tile.xMin() << "\" y=\"" << tile.yMin()
          << "\" width=\"" << tile.dx() << "\" height=\"" << tile.dy()
          << "\" fill=\"" << color << "\" stroke=\"" << color << "\"/>\n";
    }
    svg << "  </g>\n";
  }
  svg << std::fixed << std::setprecision(3);
  if (show_tile_values) {
    svg << "  <g fill=\"#d32f2f\" font-family=\"sans-serif\" font-size=\""
        << std::max(1, std::min(width, height) / 140)
        << "\" text-anchor=\"middle\">\n";
  }
  int64_t total_metal_area = 0;
  for (size_t tile_index = 0; tile_index < tiles_.size(); tile_index++) {
    const auto& tile = tiles_[tile_index];
    Polygon90Set tile_polygon;
    tile_polygon.insert(makeRectangle(tile));
    const int64_t metal_area
        = boost::polygon::area(displayed_metal & tile_polygon);
    const int64_t wire_area = boost::polygon::area(metal_shapes & tile_polygon);
    const int64_t fill_area
        = placed_fill_shapes == nullptr
              ? 0
              : boost::polygon::area(*placed_fill_shapes & tile_polygon);
    total_metal_area += metal_area;
    const double dbu_per_um2
        = static_cast<double>(dbu_per_micron) * dbu_per_micron;
    if (show_tile_values) {
      const TileViolation* tile_violation = nullptr;
      if (tile_violations != nullptr) {
        for (const TileViolation& violation : *tile_violations) {
          if (violation.tile_index == tile_index) {
            tile_violation = &violation;
            break;
          }
        }
      }
      const int text_x = (tile.xMin() + tile.xMax()) / 2;
      const int text_y = (tile.yMin() + tile.yMax()) / 2;
      const int font_size = std::max(1, std::min(width, height) / 140);
      svg << "    <text x=\"" << text_x << "\" y=\"" << text_y - font_size / 2
          << "\">tile #" << tile_index << "<tspan x=\"" << text_x << "\" dy=\""
          << font_size << "\">";
      const bool show_fillable_areas = placed_fill_shapes == nullptr
                                       && bloated_non_fill_areas != nullptr
                                       && fillable_region_areas != nullptr;
      if (show_fillable_areas) {
        svg << "wire=" << wire_area / dbu_per_um2 << " um2 ("
            << wire_area / static_cast<double>(tile.area())
            << ")</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">bloat="
            << bloated_non_fill_areas->at(tile_index) / dbu_per_um2 << " um2 ("
            << bloated_non_fill_areas->at(tile_index) / tile.area()
            << ")</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">fillable="
            << fillable_region_areas->at(tile_index) / dbu_per_um2 << " um2 ("
            << fillable_region_areas->at(tile_index) / tile.area() << ')';
        if (target_tile_densities != nullptr) {
          svg << "</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
              << "\">target="
              << target_tile_densities->at(tile_index) * tile.area()
                     / dbu_per_um2
              << " um2 (" << target_tile_densities->at(tile_index) << ')';
        }
        svg << "</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">tile_area=" << tile.area() / dbu_per_um2 << " um2";
      } else if (target_tile_densities != nullptr) {
        const double actual_density
            = metal_area / static_cast<double>(tile.area());
        const double target_density = target_tile_densities->at(tile_index);
        const double shortage = std::max(target_density - actual_density, 0.0);
        svg << "target=" << target_density * tile.area() / dbu_per_um2
            << " um2 (" << target_density << ")</tspan><tspan x=\"" << text_x
            << "\" dy=\"" << font_size
            << "\">actual=" << metal_area / dbu_per_um2 << " um2 ("
            << actual_density << ")</tspan><tspan x=\"" << text_x << "\" dy=\""
            << font_size
            << "\">shortage=" << shortage * tile.area() / dbu_per_um2
            << " um2 (" << shortage << ')';
      } else if (planned_fill_areas == nullptr) {
        svg << metal_area / dbu_per_um2 << " um2";
      } else {
        const double post_fill_density
            = (metal_area + planned_fill_areas->at(tile_index)) / tile.area();
        svg << "density=" << post_fill_density;
      }
      if (tile_violation != nullptr) {
        if (tile_violation->reason == TileViolationReason::kCapacity) {
          svg << "</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
              << "\">reason=capacity required="
              << tile_violation->required_density
              << " capacity=" << tile_violation->available_density;
        } else {
          svg << "</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
              << "\">reason=discrete target="
              << tile_violation->required_density
              << " actual=" << tile_violation->available_density;
        }
      }
      if (placed_fill_shapes != nullptr) {
        svg << "</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">wire=" << wire_area / dbu_per_um2 << " um2 ("
            << wire_area / static_cast<double>(tile.area())
            << ")</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">fill=" << fill_area / dbu_per_um2 << " um2 ("
            << fill_area / static_cast<double>(tile.area())
            << ")</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">tile_area=" << tile.area() / dbu_per_um2 << " um2";
      } else if (!show_fillable_areas && bloated_non_fill_areas != nullptr) {
        svg << "</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">wire=" << wire_area / dbu_per_um2
            << " um2</tspan><tspan x=\"" << text_x << "\" dy=\"" << font_size
            << "\">bloat="
            << bloated_non_fill_areas->at(tile_index) / dbu_per_um2 << " um2";
      }
      svg << "</tspan></text>\n";
    }
  }
  if (show_tile_values) {
    svg << "  </g>\n";
  }
  const int title_font_size = std::max(1, std::min(width, height) / 35);
  svg << "  <text x=\"" << (region_.xMin() + region_.xMax()) / 2 << "\" y=\""
      << region_.yMin() + title_font_size * 2
      << "\" fill=\"black\" font-family=\"sans-serif\" font-size=\""
      << title_font_size
      << "\" font-weight=\"bold\" text-anchor=\"middle\">Total metal area: "
      << total_metal_area / static_cast<double>(dbu_per_micron * dbu_per_micron)
      << " um2</text>\n";
  svg << "  <g fill=\"none\" stroke=\"#4e79a7\" stroke-width=\"" << stroke_width
      << "\">\n";
  for (const auto& tile : tiles_) {
    svg << "    <rect x=\"" << tile.xMin() << "\" y=\"" << tile.yMin()
        << "\" width=\"" << tile.dx() << "\" height=\"" << tile.dy()
        << "\"/>\n";
  }
  svg << "  </g>\n";
  if (window_violations != nullptr && !window_violations->empty()) {
    svg << "  <g fill=\"none\" stroke=\"#8e24aa\" stroke-width=\""
        << stroke_width * 3 << "\">\n";
    for (const WindowViolation& violation : *window_violations) {
      if (violation.window_index >= windows_.size()) {
        continue;
      }
      const odb::Rect& window = windows_[violation.window_index].bounds;
      svg << "    <rect x=\"" << window.xMin() << "\" y=\"" << window.yMin()
          << "\" width=\"" << window.dx() << "\" height=\"" << window.dy()
          << "\"/>\n";
    }
    svg << "  </g>\n  <g fill=\"#8e24aa\" font-family=\"sans-serif\" "
           "font-size=\""
        << std::max(1, std::min(width, height) / 180)
        << "\" text-anchor=\"middle\">\n";
    for (const WindowViolation& violation : *window_violations) {
      if (violation.window_index >= windows_.size()) {
        continue;
      }
      const odb::Rect& window = windows_[violation.window_index].bounds;
      svg << "    <text x=\"" << (window.xMin() + window.xMax()) / 2
          << "\" y=\"" << (window.yMin() + window.yMax()) / 2
          << "\">window=" << violation.window_index
          << " required=" << violation.required_fill_area
          << " budget=" << violation.fill_budget << "</text>\n";
    }
    svg << "  </g>\n";
  }
  svg << "</svg>\n";
  return svg.good();
}

bool TileGrid::writeLpDensityMaps(
    const std::string& filename,
    const boost::polygon::polygon_90_set_data<int>& metal_shapes,
    const std::vector<double>& planned_fill_areas) const
{
  if (planned_fill_areas.size() != tiles_.size() || tile_columns_ == 0) {
    return false;
  }

  const size_t tile_rows = tiles_.size() / tile_columns_;
  std::vector<DensityMapCell> tile_cells;
  tile_cells.reserve(tiles_.size());
  for (size_t index = 0; index < tiles_.size(); index++) {
    const double tile_area = tiles_[index].area();
    tile_cells.push_back(
        {index % tile_columns_,
         index / tile_columns_,
         tiles_[index],
         metal_areas_[index] / tile_area,
         (metal_areas_[index] + planned_fill_areas[index]) / tile_area});
  }
  if (!writeDensityMap(filename + "_tile_density.svg",
                       "Tile density map",
                       tile_cells,
                       tile_columns_,
                       tile_rows,
                       metal_shapes,
                       region_,
                       true)) {
    return false;
  }

  size_t min_column = windows_.front().first_tile_x;
  size_t max_column = min_column;
  size_t min_row = windows_.front().first_tile_y;
  size_t max_row = min_row;
  for (const DensityWindow& window : windows_) {
    min_column = std::min(min_column, window.first_tile_x);
    max_column = std::max(max_column, window.first_tile_x);
    min_row = std::min(min_row, window.first_tile_y);
    max_row = std::max(max_row, window.first_tile_y);
  }
  std::vector<DensityMapCell> window_cells;
  window_cells.reserve(windows_.size());
  for (const DensityWindow& window : windows_) {
    window_cells.push_back({window.first_tile_x - min_column,
                            window.first_tile_y - min_row,
                            window.bounds,
                            window.density,
                            window.post_fill_density});
  }
  return writeDensityMap(filename + "_window_density.svg",
                         "Sliding-window density map",
                         window_cells,
                         max_column - min_column + 1,
                         max_row - min_row + 1,
                         metal_shapes,
                         region_,
                         false);
}

void TileGrid::calculateMetalDensities(
    const boost::polygon::polygon_90_set_data<int>& metal_shapes)
{
  metal_densities_.clear();
  metal_densities_.reserve(tiles_.size());
  metal_areas_.clear();
  metal_areas_.reserve(tiles_.size());

  for (const odb::Rect& tile : tiles_) {
    Polygon90Set tile_polygon;
    tile_polygon.insert(makeRectangle(tile));
    Polygon90Set covered_area = metal_shapes & tile_polygon;

    const double tile_area = static_cast<double>(tile.area());
    const double metal_area = boost::polygon::area(covered_area);
    metal_areas_.push_back(metal_area);
    metal_densities_.push_back(tile_area == 0.0 ? 0.0 : metal_area / tile_area);
  }

  calculateWindowDensities();
}

void TileGrid::calculateWindowDensities()
{
  if (tile_columns_ == 0) {
    return;
  }

  const size_t tile_rows = tiles_.size() / tile_columns_;
  const size_t prefix_columns = tile_columns_ + 1;
  std::vector<double> prefix((tile_rows + 1) * prefix_columns, 0.0);
  const auto prefixAt
      = [&prefix, prefix_columns](size_t row, size_t column) -> double& {
    return prefix[row * prefix_columns + column];
  };

  for (size_t row = 1; row <= tile_rows; row++) {
    for (size_t column = 1; column <= tile_columns_; column++) {
      prefixAt(row, column)
          = metal_areas_[(row - 1) * tile_columns_ + column - 1]
            + prefixAt(row - 1, column) + prefixAt(row, column - 1)
            - prefixAt(row - 1, column - 1);
    }
  }

  for (DensityWindow& window : windows_) {
    const size_t first_x = window.first_tile_x;
    const size_t first_y = window.first_tile_y;
    const size_t last_x = first_x + tiles_per_window_;
    const size_t last_y = first_y + tiles_per_window_;
    window.metal_area = prefixAt(last_y, last_x) - prefixAt(first_y, last_x)
                        - prefixAt(last_y, first_x)
                        + prefixAt(first_y, first_x);
    const double window_area = static_cast<double>(window.bounds.area());
    window.density = window_area == 0.0 ? 0.0 : window.metal_area / window_area;
    window.planned_fill_area = 0.0;
    window.post_fill_density = 0.0;
  }
}

double TileGrid::totalMetalArea() const
{
  double area = 0.0;
  for (const double metal_area : metal_areas_) {
    area += metal_area;
  }
  return area;
}

}  // namespace fin

#include "FillUtill.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

#include "boost/polygon/polygon.hpp"

namespace fin {

namespace {

using Rectangle = boost::polygon::rectangle_data<int>;
using Polygon90Set = boost::polygon::polygon_90_set_data<int>;
using boost::polygon::operators::operator&;

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

struct DensityMapCell
{
  size_t column;
  size_t row;
  odb::Rect bounds;
  double metal_density;
  double lp_density;
};

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
  std::vector<std::vector<size_t>> window_tiles;
  window_tiles.reserve(windows_.size());
  for (const DensityWindow& window : windows_) {
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

bool TileGrid::writeSvg(
    const std::string& filename,
    const boost::polygon::polygon_90_set_data<int>& metal_shapes,
    int dbu_per_micron,
    const std::vector<double>* planned_fill_areas,
    bool show_tile_values) const
{
  if (planned_fill_areas != nullptr
      && planned_fill_areas->size() != tiles_.size()) {
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
  svg << std::fixed << std::setprecision(3);
  if (show_tile_values) {
    svg << "  <g fill=\"black\" font-family=\"sans-serif\" font-size=\""
        << std::max(1, std::min(width, height) / 140)
        << "\" text-anchor=\"middle\">\n";
  }
  int64_t total_metal_area = 0;
  for (size_t tile_index = 0; tile_index < tiles_.size(); tile_index++) {
    const auto& tile = tiles_[tile_index];
    Polygon90Set tile_polygon;
    tile_polygon.insert(makeRectangle(tile));
    const int64_t metal_area
        = boost::polygon::area(metal_shapes & tile_polygon);
    total_metal_area += metal_area;
    const double dbu_per_um2
        = static_cast<double>(dbu_per_micron) * dbu_per_micron;
    if (show_tile_values) {
      svg << "    <text x=\"" << (tile.xMin() + tile.xMax()) / 2 << "\" y=\""
          << (tile.yMin() + tile.yMax()) / 2 << "\">";
      if (planned_fill_areas == nullptr) {
        svg << metal_area / dbu_per_um2 << " um2";
      } else {
        const double post_fill_density
            = (metal_area + planned_fill_areas->at(tile_index)) / tile.area();
        svg << "density=" << post_fill_density;
      }
      svg << "</text>\n";
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
  svg << "  </g>\n  <g fill=\"none\" stroke=\"#e15759\" stroke-width=\""
      << stroke_width * 2 << "\">\n";
  for (const auto& window : windows_) {
    svg << "    <rect x=\"" << window.bounds.xMin() << "\" y=\""
        << window.bounds.yMin() << "\" width=\"" << window.bounds.dx()
        << "\" height=\"" << window.bounds.dy() << "\"/>\n";
  }
  svg << "  </g>\n";
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

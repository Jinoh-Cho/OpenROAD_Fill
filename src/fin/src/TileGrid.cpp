#include "fin/TileGrid.h"

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

}  // namespace

TileGrid::TileGrid(const TileGridConfig& config) : region_(config.region)
{
  if (config.resolution <= 0 || config.window_size < config.resolution
      || config.window_size % config.resolution != 0) {
    throw std::invalid_argument(
        "TileGrid requires window_size to be divisible by a positive "
        "resolution.");
  }

  const int tile_size = config.window_size / config.resolution;
  const int first_y
      = alignedStart(config.region.yMin(), config.origin.y(), tile_size);
  const int first_x
      = alignedStart(config.region.xMin(), config.origin.x(), tile_size);
  for (int y = first_y; y < config.region.yMax(); y += tile_size) {
    for (int x = first_x; x < config.region.xMax(); x += tile_size) {
      tiles_.emplace_back(std::max(x, config.region.xMin()),
                          std::max(y, config.region.yMin()),
                          std::min(x + tile_size, config.region.xMax()),
                          std::min(y + tile_size, config.region.yMax()));
    }
  }

  tile_to_window_.resize(tiles_.size());
  const int first_window_y = alignedStart(
      config.region.yMin(), config.origin.y(), config.window_size);
  const int first_window_x = alignedStart(
      config.region.xMin(), config.origin.x(), config.window_size);
  for (int y = first_window_y; y < config.region.yMax();
       y += config.window_size) {
    for (int x = first_window_x; x < config.region.xMax();
         x += config.window_size) {
      DensityWindow window{
          odb::Rect(x, y, x + config.window_size, y + config.window_size), {}};
      const size_t window_index = windows_.size();
      for (size_t tile_index = 0; tile_index < tiles_.size(); tile_index++) {
        if (tiles_[tile_index].overlaps(window.bounds)) {
          window.tile_indices.push_back(tile_index);
          tile_to_window_[tile_index] = window_index;
        }
      }
      windows_.push_back(std::move(window));
    }
  }
}

bool TileGrid::writeSvg(
    const std::string& filename,
    const boost::polygon::polygon_90_set_data<int>& metal_shapes,
    int dbu_per_micron) const
{
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
  svg << "  <g fill=\"black\" font-family=\"sans-serif\" font-size=\""
      << std::max(1, std::min(width, height) / 140)
      << "\" text-anchor=\"middle\">\n";
  int64_t total_metal_area = 0;
  for (const auto& tile : tiles_) {
    Polygon90Set tile_polygon;
    tile_polygon.insert(makeRectangle(tile));
    const int64_t metal_area
        = boost::polygon::area(metal_shapes & tile_polygon);
    total_metal_area += metal_area;
    const double area_um2
        = metal_area / static_cast<double>(dbu_per_micron * dbu_per_micron);
    svg << "    <text x=\"" << (tile.xMin() + tile.xMax()) / 2 << "\" y=\""
        << (tile.yMin() + tile.yMax()) / 2 << "\">" << area_um2
        << " um2</text>\n";
  }
  svg << "  </g>\n";
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
  svg << "  </g>\n</svg>\n";
  return svg.good();
}

void TileGrid::calculateMetalDensities(
    const boost::polygon::polygon_90_set_data<int>& metal_shapes)
{
  metal_densities_.clear();
  metal_densities_.reserve(tiles_.size());

  for (const odb::Rect& tile : tiles_) {
    Polygon90Set tile_polygon;
    tile_polygon.insert(makeRectangle(tile));
    Polygon90Set covered_area = metal_shapes & tile_polygon;

    const double tile_area = static_cast<double>(tile.area());
    metal_densities_.push_back(
        tile_area == 0.0 ? 0.0
                         : boost::polygon::area(covered_area) / tile_area);
  }
}

double TileGrid::totalMetalArea() const
{
  double area = 0.0;
  for (size_t index = 0; index < tiles_.size(); index++) {
    area += metal_densities_.at(index) * tiles_[index].area();
  }
  return area;
}

}  // namespace fin

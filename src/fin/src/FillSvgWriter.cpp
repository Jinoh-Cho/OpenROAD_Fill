// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillSvgWriter.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fin {
using odb::Rect;
namespace {
using Rectangle = boost::polygon::rectangle_data<int>;
using Polygon90Set = boost::polygon::polygon_90_set_data<int>;
using boost::polygon::operators::operator&;
using boost::polygon::operators::operator+=;
Rectangle makeRectangle(const odb::Rect& rect)
{
  return Rectangle(rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax());
}

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

}  // namespace

bool writeTileGridSvg(
    const TileGrid& grid,
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
    const std::vector<double>* fillable_region_areas)
{
  if (planned_fill_areas != nullptr
      && planned_fill_areas->size() != grid.tiles().size()) {
    return false;
  }
  if (target_tile_densities != nullptr
      && target_tile_densities->size() != grid.tiles().size()) {
    return false;
  }
  if (bloated_non_fill_areas != nullptr
      && bloated_non_fill_areas->size() != grid.tiles().size()) {
    return false;
  }
  if (fillable_region_areas != nullptr
      && fillable_region_areas->size() != grid.tiles().size()) {
    return false;
  }
  std::ofstream svg(filename);
  if (!svg) {
    return false;
  }

  odb::Rect display_bounds = grid.region();
  for (const auto& window : grid.windows()) {
    display_bounds.merge(window.bounds);
  }
  const int width = display_bounds.dx();
  const int height = display_bounds.dy();
  const int stroke_width = std::max(1, std::min(width, height) / 500);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
      << display_bounds.xMin() << ' ' << display_bounds.yMin() << ' ' << width
      << ' ' << height << "\">\n";
  svg << "  <rect x=\"" << grid.region().xMin() << "\" y=\""
      << grid.region().yMin() << "\" width=\"" << grid.region().dx()
      << "\" height=\"" << grid.region().dy()
      << "\" fill=\"white\" stroke=\"black\"/>\n";
  Polygon90Set region_polygon;
  region_polygon.insert(makeRectangle(grid.region()));
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
      if (violation.tile_index >= grid.tiles().size()) {
        continue;
      }
      const odb::Rect& tile = grid.tiles()[violation.tile_index];
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
    svg << "  <g fill=\"#d32f2f\" font-family=\"sans-serif\" "
           "text-anchor=\"middle\">\n";
  }
  int64_t total_metal_area = 0;
  for (size_t tile_index = 0; tile_index < grid.tiles().size(); tile_index++) {
    const auto& tile = grid.tiles()[tile_index];
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
      const bool show_fillable_areas = placed_fill_shapes == nullptr
                                       && bloated_non_fill_areas != nullptr
                                       && fillable_region_areas != nullptr;
      const auto format = [](double value) {
        std::ostringstream text;
        text << std::fixed << std::setprecision(3) << value;
        return text.str();
      };
      std::vector<std::string> text_lines;
      text_lines.push_back("tile #" + std::to_string(tile_index));
      if (show_fillable_areas) {
        text_lines.push_back(
            "wire=" + format(wire_area / dbu_per_um2) + " um2 ("
            + format(wire_area / static_cast<double>(tile.area())) + ')');
        text_lines.push_back(
            "bloat="
            + format(bloated_non_fill_areas->at(tile_index) / dbu_per_um2)
            + " um2 ("
            + format(bloated_non_fill_areas->at(tile_index) / tile.area())
            + ')');
        text_lines.push_back(
            "fillable="
            + format(fillable_region_areas->at(tile_index) / dbu_per_um2)
            + " um2 ("
            + format(fillable_region_areas->at(tile_index) / tile.area())
            + ')');
        if (target_tile_densities != nullptr) {
          text_lines.push_back(
              "target="
              + format(target_tile_densities->at(tile_index) * tile.area()
                       / dbu_per_um2)
              + " um2 (" + format(target_tile_densities->at(tile_index)) + ')');
        }
        text_lines.push_back("tile_area=" + format(tile.area() / dbu_per_um2)
                             + " um2");
      } else if (target_tile_densities != nullptr) {
        const double actual_density
            = metal_area / static_cast<double>(tile.area());
        const double target_density = target_tile_densities->at(tile_index);
        const double shortage = std::max(target_density - actual_density, 0.0);
        text_lines.push_back(
            "target=" + format(target_density * tile.area() / dbu_per_um2)
            + " um2 (" + format(target_density) + ')');
        text_lines.push_back("actual=" + format(metal_area / dbu_per_um2)
                             + " um2 (" + format(actual_density) + ')');
        text_lines.push_back("shortage="
                             + format(shortage * tile.area() / dbu_per_um2)
                             + " um2 (" + format(shortage) + ')');
      } else if (planned_fill_areas == nullptr) {
        text_lines.push_back(format(metal_area / dbu_per_um2) + " um2");
      } else {
        const double post_fill_density
            = (metal_area + planned_fill_areas->at(tile_index)) / tile.area();
        text_lines.push_back("density=" + format(post_fill_density));
      }
      if (tile_violation != nullptr) {
        if (tile_violation->reason == TileViolationReason::kCapacity) {
          text_lines.push_back("reason=capacity required="
                               + format(tile_violation->required_density)
                               + " capacity="
                               + format(tile_violation->available_density));
        } else {
          text_lines.push_back("reason=discrete target="
                               + format(tile_violation->required_density)
                               + " actual="
                               + format(tile_violation->available_density));
        }
      }
      if (placed_fill_shapes != nullptr) {
        text_lines.push_back(
            "wire=" + format(wire_area / dbu_per_um2) + " um2 ("
            + format(wire_area / static_cast<double>(tile.area())) + ')');
        text_lines.push_back(
            "fill=" + format(fill_area / dbu_per_um2) + " um2 ("
            + format(fill_area / static_cast<double>(tile.area())) + ')');
        text_lines.push_back("tile_area=" + format(tile.area() / dbu_per_um2)
                             + " um2");
      } else if (!show_fillable_areas && bloated_non_fill_areas != nullptr) {
        text_lines.push_back("wire=" + format(wire_area / dbu_per_um2)
                             + " um2");
        text_lines.push_back(
            "bloat="
            + format(bloated_non_fill_areas->at(tile_index) / dbu_per_um2)
            + " um2");
      }
      size_t longest_line = 0;
      for (const std::string& line : text_lines) {
        longest_line = std::max(longest_line, line.size());
      }
      constexpr double kCharacterWidth = 0.60;
      constexpr double kLineHeight = 1.20;
      const double global_font_size
          = std::max(1, std::min(width, height) / 140);
      const double width_limited_font_size
          = 0.85 * tile.dx() / (kCharacterWidth * longest_line);
      const double height_limited_font_size
          = 0.85 * tile.dy() / (kLineHeight * text_lines.size());
      const double font_size = std::max(1.0,
                                        std::min({global_font_size,
                                                  width_limited_font_size,
                                                  height_limited_font_size}));
      const double text_y
          = (tile.yMin() + tile.yMax()) / 2.0
            - (text_lines.size() - 1) * kLineHeight * font_size / 2.0;
      svg << "    <text x=\"" << text_x << "\" y=\"" << text_y
          << "\" font-size=\"" << font_size << "\">";
      for (size_t line_index = 0; line_index < text_lines.size();
           line_index++) {
        if (line_index != 0) {
          svg << "<tspan x=\"" << text_x << "\" dy=\""
              << kLineHeight * font_size << "\">";
        }
        svg << text_lines[line_index];
        if (line_index != 0) {
          svg << "</tspan>";
        }
      }
      svg << "</text>\n";
    }
  }
  if (show_tile_values) {
    svg << "  </g>\n";
  }
  const int title_font_size = std::max(1, std::min(width, height) / 35);
  svg << "  <text x=\"" << (grid.region().xMin() + grid.region().xMax()) / 2
      << "\" y=\"" << grid.region().yMin() + title_font_size * 2
      << "\" fill=\"black\" font-family=\"sans-serif\" font-size=\""
      << title_font_size
      << "\" font-weight=\"bold\" text-anchor=\"middle\">Total metal area: "
      << total_metal_area / static_cast<double>(dbu_per_micron * dbu_per_micron)
      << " um2</text>\n";
  svg << "  <g fill=\"none\" stroke=\"#4e79a7\" stroke-width=\"" << stroke_width
      << "\">\n";
  for (const auto& tile : grid.tiles()) {
    svg << "    <rect x=\"" << tile.xMin() << "\" y=\"" << tile.yMin()
        << "\" width=\"" << tile.dx() << "\" height=\"" << tile.dy()
        << "\"/>\n";
  }
  svg << "  </g>\n";
  if (window_violations != nullptr && !window_violations->empty()) {
    svg << "  <g fill=\"none\" stroke=\"#8e24aa\" stroke-width=\""
        << stroke_width * 3 << "\">\n";
    for (const WindowViolation& violation : *window_violations) {
      if (violation.window_index >= grid.windows().size()) {
        continue;
      }
      const odb::Rect& window = grid.windows()[violation.window_index].bounds;
      svg << "    <rect x=\"" << window.xMin() << "\" y=\"" << window.yMin()
          << "\" width=\"" << window.dx() << "\" height=\"" << window.dy()
          << "\"/>\n";
    }
    svg << "  </g>\n  <g fill=\"#8e24aa\" font-family=\"sans-serif\" "
           "font-size=\""
        << std::max(1, std::min(width, height) / 180)
        << "\" text-anchor=\"middle\">\n";
    for (const WindowViolation& violation : *window_violations) {
      if (violation.window_index >= grid.windows().size()) {
        continue;
      }
      const odb::Rect& window = grid.windows()[violation.window_index].bounds;
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

bool writeFillAreaSvg(const std::string& filename,
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

}  // namespace fin

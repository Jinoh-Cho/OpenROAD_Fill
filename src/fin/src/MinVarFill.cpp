// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "MinVarFill.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "FillConfig.h"
#include "FillGeometry.h"
#include "fin/TileGrid.h"
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
  Polygon90Set fill_area;
  fill_area += area;
  const bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  const auto [space_x, space_y] = getSpacing(layer, config);
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
      const int mask_count = std::max(num_masks, 1);
      int index = 0;
      for (const auto& rectangle : rectangles) {
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
    fill_area -= all_shape_fills;
  }
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

double MinVarFill::tileGridMetalArea(const char* rules_filename,
                                     const Rect& region,
                                     const odb::Point& origin,
                                     int window_size,
                                     int resolution,
                                     const char* svg_filename)
{
  const auto layers
      = loadFillLayerConfigs(rules_filename, db_->getTech(), logger_);
  auto* block = db_->getChip()->getBlock();
  TileGrid grid({region, origin, window_size, resolution});
  double total_area = 0.0;
  for (auto* layer : db_->getTech()->getLayers()) {
    if (layers.find(layer) == layers.end()) {
      continue;
    }
    const Polygon90Set layer_shapes = orNonFills(block, layer);
    grid.calculateMetalDensities(layer_shapes);
    const double layer_area = grid.totalMetalArea();
    logger_->info(
        FIN,
        17,
        "MinVar tile grid: layer={}, tiles={}, metal_area={:.0f} DBU^2.",
        layer->getConstName(),
        grid.tiles().size(),
        layer_area);
    total_area += layer_area;
    if (svg_filename != nullptr && svg_filename[0] != '\0') {
      grid.writeSvg(
          std::string(svg_filename) + "_" + layer->getConstName() + ".svg",
          layer_shapes,
          db_->getTech()->getDbUnitsPerMicron());
    }
  }
  return total_area;
}

}  // namespace fin

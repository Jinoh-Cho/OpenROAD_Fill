// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "LPFillUtil.h"

#include <algorithm>
#include <string>

#include "FillGeometry.h"
#include "graphics.h"

namespace fin::lpfill {
using odb::dbTechLayer;
using odb::dbTechLayerDir;
using odb::Rect;

std::pair<int, int> getSpacing(dbTechLayer* layer, const FillShapesConfig& cfg)
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

void prune(Polygon90Set& fill_area,
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

std::vector<Rectangle> makeFillCandidates(const Polygon90& area,
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

std::vector<Rectangle> makeTileFillCandidates(const Rect& tile,
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

Polygon90Set makeTileFillArea(const Rect& tile,
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

}  // namespace fin::lpfill

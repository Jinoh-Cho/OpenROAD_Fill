// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
// Frozen headless reference before skipping the final size's unused-area
// update.
#include "CandidateReference.h"

#include "FillGeometry.h"
#include "graphics.h"
namespace fin::lpfill::test_reference {
using odb::dbTechLayer;
using odb::dbTechLayerDir;
std::vector<Rectangle> makeFillCandidatesBefore(const Polygon90& area,
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
    if (candidateTraceEnabled()) {
      traceCandidate("shape begin width=" + std::to_string(width)
                     + " height=" + std::to_string(height));
    }
    traceCandidate("shape.copy begin");
    Polygon90Set pruned_fill_area = fill_area;
    traceCandidate("shape.copy end; shape.shrink begin");
    const int east_west_sizing = width / 2 - 1;
    const int north_south_sizing = height / 2 - 1;
    shrink(pruned_fill_area,
           east_west_sizing,
           east_west_sizing,
           north_south_sizing,
           north_south_sizing);
    traceCandidate("shape.shrink end; shape.bloat begin");
    bloat(pruned_fill_area,
          east_west_sizing,
          east_west_sizing,
          north_south_sizing,
          north_south_sizing);
    traceCandidate("shape.bloat end; shape.prune begin");
    prune(pruned_fill_area, layer, config, graphics);
    traceCandidate("shape.prune end");
    if (graphics != nullptr) {
      graphics->status("Fill Area for " + std::to_string(width) + " "
                       + std::to_string(height));
      graphics->drawPolygon90Set(pruned_fill_area);
    }

    Polygon90Set all_shape_fills;
    std::vector<Polygon90> sub_fill_areas;
    traceCandidate("shape.extract_polygons begin");
    pruned_fill_area.get(sub_fill_areas);
    traceCandidate("shape.extract_polygons end");
    for (const auto& sub_fill_area : sub_fill_areas) {
      if (candidateTraceEnabled()) {
        traceCandidate("subpolygon begin index="
                       + std::to_string(&sub_fill_area - sub_fill_areas.data())
                       + " count=" + std::to_string(sub_fill_areas.size()));
      }
      Rectangle bounds;
      traceCandidate("subpolygon.extents begin");
      extents(bounds, sub_fill_area);
      traceCandidate(
          "subpolygon.extents end; subpolygon.tile_rectangles begin");
      Polygon90Set tiled_fills;
      for (int x = xl(bounds); x < xh(bounds); x += width + space_x) {
        for (int y = yl(bounds); y < yh(bounds); y += height + space_y) {
          tiled_fills.insert(makeRect(x, y, x + width, y + height));
        }
      }
      traceCandidate(
          "subpolygon.tile_rectangles end; subpolygon.intersect begin");
      Polygon90Set fills = tiled_fills & sub_fill_area;
      traceCandidate("subpolygon.intersect end; subpolygon.keep begin");
      keep(fills,
           width * height,
           width * height,
           width - 1,
           width,
           height - 1,
           height);
      traceCandidate("subpolygon.keep end; subpolygon.bloat_accumulate begin");
      Polygon90Set bloated_fills(fills);
      all_shape_fills
          += bloat(bloated_fills, space_x, space_x, space_y, space_y);
      traceCandidate(
          "subpolygon.bloat_accumulate end; subpolygon.get_rectangles begin");

      std::vector<Rectangle> rectangles;
      fills.get_rectangles(rectangles);
      traceCandidate("subpolygon.get_rectangles end; subpolygon.append begin");
      candidates.insert(candidates.end(), rectangles.begin(), rectangles.end());
      traceCandidate("subpolygon.append end; subpolygon end");
    }
    traceCandidate("shape.subtract_used begin");
    fill_area -= all_shape_fills;
    traceCandidate("shape.subtract_used end; shape end");
  }
  return candidates;
}

std::vector<Rectangle> makeTileFillCandidatesBefore(
    const Polygon90Set& tile_area,
    dbTechLayer* layer,
    const FillShapesConfig& config,
    Graphics* graphics)
{
  traceCandidate("tile.extract_polygons begin");

  std::vector<Polygon90> polygons;
  tile_area.get(polygons);
  traceCandidate("tile.extract_polygons end");
  std::vector<Rectangle> candidates;
  for (const Polygon90& polygon : polygons) {
    if (candidateTraceEnabled()) {
      traceCandidate("polygon begin index="
                     + std::to_string(&polygon - polygons.data())
                     + " count=" + std::to_string(polygons.size()));
    }
    std::vector<Rectangle> polygon_candidates
        = makeFillCandidatesBefore(polygon, layer, config, graphics);
    candidates.insert(
        candidates.end(), polygon_candidates.begin(), polygon_candidates.end());
    traceCandidate("polygon end");
  }
  return candidates;
}

}  // namespace fin::lpfill::test_reference

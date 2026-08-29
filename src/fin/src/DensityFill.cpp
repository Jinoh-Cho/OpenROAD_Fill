// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#include "DensityFill.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FillGeometry.h"
#include "boost/polygon/polygon.hpp"
#include "graphics.h"
#include "odb/db.h"
#include "odb/dbShape.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"
#include "polygon.h"
#include "utl/Logger.h"

namespace fin {

using utl::FIN;

using odb::dbBlock;
using odb::dbChip;
using odb::dbDatabase;
using odb::dbFill;
using odb::dbTech;
using odb::dbTechLayer;
using odb::dbTechLayerDir;
using odb::Rect;

////////////////////////////////////////////////////////////////

DensityFill::DensityFill(dbDatabase* db, utl::Logger* logger, bool debug)
    : db_(db), logger_(logger)
{
  if (debug && Graphics::guiActive()) {
    graphics_ = std::make_unique<Graphics>();
  }
}

// must be in the .cpp due to forward decl
DensityFill::~DensityFill() = default;

static std::pair<int, int> getSpacing(dbTechLayer* layer,
                                      const FillShapesConfig& cfg)
{
  bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  int space_x = cfg.space_to_fill;
  int space_y = space_x;
  if (is_horiz) {
    space_x = std::max(space_x, cfg.space_line_end);
  } else {
    space_y = std::max(space_y, cfg.space_line_end);
  }

  return std::make_pair(space_x, space_y);
}

// Two different polygons might be less than min space apart and this
// can lead to DRVs when they are filled independently.  To avoid this
// we exclude a min-space area around each polygon.  This is somewhat
// conservative as we may not actually put a fill where a DRV would be
// caused but is much faster than updating the fill area after every
// polygon is filled.
static void prune(Polygon90Set& fill_area,
                  dbTechLayer* layer,
                  const FillShapesConfig& cfg,
                  Graphics* graphics)
{
  auto [space_x, space_y] = getSpacing(layer, cfg);

  // From Boost on grow_and:
  //   Same as bloating non-overlapping regions and then applying self
  //   intersect to retain only the overlaps introduced by the bloat.
  Polygon90Set pruned(fill_area);
  grow_and(pruned, space_x, space_x, space_y, space_y);
  fill_area -= pruned;
}

// Fill a polygon (area) on the given layer using the given configuration.
// Num_masks is used to color the generated fills.
// filled_area, if given, is an OR of the generated fills without bloating
static void fillPolygon(const Polygon90& area,
                        dbTechLayer* layer,
                        dbBlock* block,
                        const FillShapesConfig& cfg,
                        int num_masks,
                        bool needs_opc,
                        Graphics* graphics,
                        Polygon90Set* filled_area = nullptr)
{
  // Convert the area polygon to a polygon set as we will remove areas
  // filled by one fill shape from consideration by future shapes,
  // which may result in the polygon breaking apart into a set of
  // remaining polygons
  Polygon90Set fill_area;
  fill_area += area;

  bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  auto [space_x, space_y] = getSpacing(layer, cfg);

  auto iter = cfg.shapes.begin();
  while (iter != cfg.shapes.end()) {
    auto [w, h] = *iter++;
    // Ensure the longer direction is in the preferred direction
    if ((is_horiz && w < h) || (!is_horiz && h < w)) {
      std::swap(w, h);
    }

    // Use a shrink/bloat cycle to remove any areas that are too small to fill
    // with this fill shape.  A benefit is that it helps break up big polygons
    // which makes it easier to fill them
    Polygon90Set pruned_fill_area = fill_area;

    int ew_sizing = w / 2 - 1;
    int ns_sizing = h / 2 - 1;
    shrink(pruned_fill_area, ew_sizing, ew_sizing, ns_sizing, ns_sizing);
    bloat(pruned_fill_area, ew_sizing, ew_sizing, ns_sizing, ns_sizing);

    // The polygon may break into parts that could be less than min-space
    // apart so prune the result.
    prune(pruned_fill_area, layer, cfg, graphics);

    if (graphics) {
      graphics->status("Fill Area for " + std::to_string(w) + " "
                       + std::to_string(h));
      graphics->drawPolygon90Set(pruned_fill_area);
    }

    Polygon90Set all_iter_fills;
    std::vector<Polygon90> sub_fill_areas;
    pruned_fill_area.get(sub_fill_areas);
    for (auto& sub_fill_area : sub_fill_areas) {
      Rectangle bounds;
      extents(bounds, sub_fill_area);

      // Tile a set of fills to cover the bounds.  (KLayout allows a
      // sweep on the origin of the tile set looking for maximum fill.
      // We could try that in the future.)
      Polygon90Set all_fills;
      for (int x = xl(bounds); x < xh(bounds); x += w + space_x) {
        for (int y = yl(bounds); y < yh(bounds); y += h + space_y) {
          all_fills.insert(makeRect(x, y, x + w, y + h));
        }
      }

      // Intersect fills with the sub area and keep only whole fill shapes
      Polygon90Set fills = all_fills & sub_fill_area;
      keep(fills, w * h, w * h, w - 1, w, h - 1, h);

      Polygon90Set tmp_fills(fills);
      all_iter_fills += bloat(tmp_fills, space_x, space_x, space_y, space_y);

      // Insert fills into the db
      std::vector<Rectangle> polygons;
      fills.get_rectangles(polygons);
      const int num_mask = std::max(num_masks, 1);
      int cnt = 0;
      for (auto& f : polygons) {
        int mask;
        if (num_mask == 1) {
          mask = 0;  // don't write a mask for single mask layers
        } else {
          mask = cnt++ % num_mask + 1;
        }
        auto x_lo = xl(f);
        auto y_lo = yl(f);
        auto x_hi = xh(f);
        auto y_hi = yh(f);
        dbFill::create(block, needs_opc, mask, layer, x_lo, y_lo, x_hi, y_hi);
        if (filled_area) {
          *filled_area += makeRect(x_lo, y_lo, x_hi, y_hi);
        }
      }
    }
    // Remove filled area from use by future shapes
    fill_area -= all_iter_fills;
  }
}

// Fill the given layer
void DensityFill::fillLayer(dbBlock* block,
                            dbTechLayer* layer,
                            const odb::Rect& fill_bounds_rect)
{
  logger_->info(FIN, 3, "Filling layer {}.", layer->getConstName());

  Polygon90Set non_fill = orNonFills(block, layer);

  auto fill_bounds = makeRect(fill_bounds_rect.xMin(),
                              fill_bounds_rect.yMin(),
                              fill_bounds_rect.xMax(),
                              fill_bounds_rect.yMax());

  const FillLayerConfig& cfg = layers_[layer];

  std::vector<Polygon90> polygons;

  // Do non-OPC fill
  Polygon90Set fill_area
      = fill_bounds - (non_fill + cfg.non_opc.space_to_non_fill);

  if (graphics_) {
    graphics_->status("Non-OPC Area");
    graphics_->drawPolygon90Set(fill_area);
  }

  prune(fill_area, layer, cfg.non_opc, graphics_.get());

  fill_area.get(polygons);
  logger_->info(FIN, 9, "Filling {} areas with non-OPC fill.", polygons.size());

  Polygon90Set non_opc_fill_area;
  for (auto& polygon : polygons) {
    fillPolygon(polygon,
                layer,
                block,
                cfg.non_opc,
                cfg.num_masks,
                false,
                graphics_.get(),
                &non_opc_fill_area);
  }
  logger_->info(FIN, 4, "Total fills: {}.", block->getFills().size());

  if (!cfg.has_opc) {
    return;
  }

  Polygon90Set opc_fill_area
      = fill_bounds - (non_fill + cfg.opc.space_to_non_fill)
        - (non_opc_fill_area + cfg.non_opc.space_to_fill);

  if (graphics_) {
    graphics_->status("OPC Area");
    graphics_->drawPolygon90Set(opc_fill_area);
  }

  prune(opc_fill_area, layer, cfg.opc, graphics_.get());

  polygons.clear();
  opc_fill_area.get(polygons);
  logger_->info(FIN, 5, "Filling {} areas with OPC fill.", polygons.size());
  for (auto& polygon : polygons) {
    fillPolygon(
        polygon, layer, block, cfg.opc, cfg.num_masks, true, graphics_.get());
  }

  logger_->info(FIN, 6, "Total fills: {}.", block->getFills().size());

  if (graphics_) {
    graphics_->status("OPC Area");
    graphics_->drawPolygon90Set(opc_fill_area);
  }
}

// Fill the design according to the given cfg file
void DensityFill::fill(const char* cfg_filename, const odb::Rect& fill_area)
{
  dbTech* tech = db_->getTech();
  layers_ = loadFillLayerConfigs(cfg_filename, tech, logger_);

  dbChip* chip = db_->getChip();
  dbBlock* block = chip->getBlock();

  for (dbTechLayer* layer : tech->getLayers()) {
    auto it = layers_.find(layer);
    if (it == layers_.end()) {
      logger_->warn(FIN, 10, "Skipping layer {}.", layer->getConstName());
      continue;
    }
    fillLayer(block, layer, fill_area);
  }
}

}  // namespace fin

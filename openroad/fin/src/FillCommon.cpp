// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2026, The OpenROAD Authors

#include "FillCommon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "boost/property_tree/json_parser.hpp"
#include "odb/dbShape.h"
#include "odb/dbTypes.h"

namespace fin {
using odb::dbBlock;
using odb::dbInstShapeItr;
using odb::dbShape;
using odb::dbTech;
using odb::dbTechLayer;
using odb::dbTechLayerDir;
using odb::dbTechVia;
using odb::dbVia;
using odb::dbWire;
using odb::dbWireShapeItr;
using odb::Rect;
using utl::FIN;
namespace pt = boost::property_tree;

// Make a boost polygon representing a rectangle
Polygon90 makeRect(const int x_lo,
                   const int y_lo,
                   const int x_hi,
                   const int y_hi)
{
  using Pt = Polygon90::point_type;
  const std::array<Pt, 4> pts
      = {Pt(x_lo, y_lo), Pt(x_hi, y_lo), Pt(x_hi, y_hi), Pt(x_lo, y_hi)};

  Polygon90 poly;
  poly.set(pts.begin(), pts.end());
  return poly;
}

// Insert into polygon_set any part of given shape on the given layer (shape may
// be a via)
static void insertShape(const dbShape& shape,
                        Polygon90Set& polygon_set,
                        dbTechLayer* layer)
{
  const auto type = shape.getType();
  switch (type) {
    case dbShape::VIA:
    case dbShape::TECH_VIA: {
      dbTechLayer* top;
      dbTechLayer* bottom;
      if (type == dbShape::VIA) {
        dbVia* via = shape.getVia();
        top = via->getTopLayer();
        bottom = via->getBottomLayer();
      } else {
        dbTechVia* via = shape.getTechVia();
        top = via->getTopLayer();
        bottom = via->getBottomLayer();
      }

      if (top != layer && bottom != layer) {
        return;
      }
      std::vector<dbShape> boxes;
      dbShape::getViaBoxes(shape, boxes);
      for (auto& box : boxes) {
        if (box.getTechLayer() == layer) {
          polygon_set.insert(
              makeRect(box.xMin(), box.yMin(), box.xMax(), box.yMax()));
        }
      }
      break;
    }
    case dbShape::SEGMENT:
      if (shape.getTechLayer() == layer) {
        polygon_set.insert(
            makeRect(shape.xMin(), shape.yMin(), shape.xMax(), shape.yMax()));
      }
      break;
    case dbShape::TECH_VIA_BOX:
    case dbShape::VIA_BOX:
      if (shape.getTechLayer() == layer) {
        polygon_set.insert(
            makeRect(shape.xMin(), shape.yMin(), shape.xMax(), shape.yMax()));
      }
      break;
  }
}

// Build a polygon set out of all the non-fill shape on the given layer
// including wires, special wires, fill obstructions, and instances' pins & OBS
Polygon90Set collectNonFillShapes(dbBlock* block,
                                  dbTechLayer* layer,
                                  const bool include_fill_obstructions)
{
  Polygon90Set non_fill;  // The result
  dbShape shape;          // Shared temp

  // Get shapes from regular wires
  dbWireShapeItr shapes;
  for (auto net : block->getNets()) {
    dbWire* wire = net->getWire();
    if (!wire) {
      continue;
    }
    for (shapes.begin(wire); shapes.next(shape);) {
      insertShape(shape, non_fill, layer);
    }
  }

  // Get shapes from special wires
  std::vector<dbShape> via_shapes;
  for (auto net : block->getNets()) {
    for (auto swire : net->getSWires()) {
      for (auto sbox : swire->getWires()) {
        if (sbox->isVia()) {
          const Rect rect = sbox->getBox();
          if (auto* via = sbox->getTechVia()) {
            shape.setVia(via, rect);
          } else {
            shape.setVia(sbox->getBlockVia(), rect);
          }
          dbShape::getViaBoxes(shape, via_shapes);
          for (auto& via_shape : via_shapes) {
            insertShape(via_shape, non_fill, layer);
          }
        } else if (sbox->getTechLayer() == layer) {
          non_fill.insert(
              makeRect(sbox->xMin(), sbox->yMin(), sbox->xMax(), sbox->yMax()));
        }
      }
    }
  }

  // Get shapes from instances
  dbInstShapeItr insts(/* expand_vias */ false);
  for (auto inst : block->getInsts()) {
    for (insts.begin(inst, dbInstShapeItr::ALL); insts.next(shape);) {
      insertShape(shape, non_fill, layer);
    }
  }

  // Get shapes from fill obstructions
  for (auto obstruction : block->getObstructions()) {
    auto* box = obstruction->getBBox();
    if (include_fill_obstructions && obstruction->isFillObstruction()
        && box->getTechLayer() == layer) {
      non_fill.insert(
          makeRect(box->xMin(), box->yMin(), box->xMax(), box->yMax()));
    }
  }

  return non_fill;
}

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

// Two different polygons might be less than min space apart and this
// can lead to DRVs when they are filled independently.  To avoid this
// we exclude a min-space area around each polygon.  This is somewhat
// conservative as we may not actually put a fill where a DRV would be
// caused but is much faster than updating the fill area after every
// polygon is filled.
void pruneFillArea(Polygon90Set& fill_area,
                   dbTechLayer* layer,
                   const FillShapesConfig& cfg)
{
  auto [space_x, space_y] = getSpacing(layer, cfg);

  // From Boost on grow_and:
  //   Same as bloating non-overlapping regions and then applying self
  //   intersect to retain only the overlaps introduced by the bloat.
  Polygon90Set pruned(fill_area);
  grow_and(pruned, space_x, space_x, space_y, space_y);
  fill_area -= pruned;
}

namespace {
int64_t floorDivide(const int64_t value, const int64_t divisor)
{
  return value / divisor - (value % divisor < 0 ? 1 : 0);
}

int64_t ceilDivide(const int64_t value, const int64_t divisor)
{
  return -floorDivide(-value, divisor);
}

std::vector<Rectangle> staggerSites(const Rectangle& bounds,
                                    const int width,
                                    const int height,
                                    const int64_t pitch_x,
                                    const int64_t pitch_y,
                                    const FillShapesConfig& config)
{
  std::vector<Rectangle> result;
  // Four rectangular sublattices encode the odd-row X and odd-column Y shifts.
  for (int row_parity = 0; row_parity < 2; ++row_parity) {
    for (int col_parity = 0; col_parity < 2; ++col_parity) {
      const int64_t base_x = int64_t{config.pattern_origin.x()}
                             + col_parity * pitch_x
                             + int64_t{row_parity} * config.stagger_offset.x();
      const int64_t base_y = int64_t{config.pattern_origin.y()}
                             + row_parity * pitch_y
                             + int64_t{col_parity} * config.stagger_offset.y();
      const int64_t first_x
          = base_x
            + ceilDivide(int64_t{xl(bounds)} - base_x, 2 * pitch_x) * 2
                  * pitch_x;
      const int64_t first_y
          = base_y
            + ceilDivide(int64_t{yl(bounds)} - base_y, 2 * pitch_y) * 2
                  * pitch_y;
      for (int64_t y = first_y; y + height <= yh(bounds); y += 2 * pitch_y) {
        for (int64_t x = first_x; x + width <= xh(bounds); x += 2 * pitch_x) {
          result.emplace_back(x, y, x + width, y + height);
        }
      }
    }
  }
  return result;
}

}  // namespace

Polygon90Set makeFillPattern(const Rectangle& bounds,
                             const int width,
                             const int height,
                             const int space_x,
                             const int space_y,
                             const FillShapesConfig& config)
{
  if (width <= 0 || height <= 0 || space_x < 0 || space_y < 0) {
    throw std::invalid_argument(
        "Fill pattern requires positive sizes and nonnegative spacing.");
  }
  if (config.stagger && config.stagger_offset.x() != 0
      && config.stagger_offset.y() != 0) {
    throw std::invalid_argument(
        "stagger_offset must select only one axis: [x, 0] or [0, y].");
  }
  const int64_t pitch_x = int64_t{width} + space_x;
  const int64_t pitch_y = int64_t{height} + space_y;
  Polygon90Set result;
  if (!config.stagger) {
    // Preserve the original grid, including partial boundary shapes used by
    // the existing union/intersection/keep sequence when spacing is zero.
    for (int64_t x = xl(bounds); x < xh(bounds); x += pitch_x) {
      for (int64_t y = yl(bounds); y < yh(bounds); y += pitch_y) {
        if (x + width <= std::numeric_limits<int>::max()
            && y + height <= std::numeric_limits<int>::max()) {
          result.insert(Rectangle(x, y, x + width, y + height));
        }
      }
    }
    return result;
  }
  auto sites = staggerSites(bounds, width, height, pitch_x, pitch_y, config);
  for (const auto& rect : sites) {
    result.insert(rect);
  }
  return result;
}

namespace {
int readDistance(const pt::ptree& tree, const char* key, const int dbu)
{
  const double value = tree.get<double>(key) * dbu;
  if (!std::isfinite(value) || value < 0
      || value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string("Invalid fill rule: ") + key);
  }
  return static_cast<int>(value);
}

FillShapesConfig readShapes(const pt::ptree& tree, const int dbu)
{
  FillShapesConfig config;
  config.space_to_fill = readDistance(tree, "space_to_fill", dbu);
  config.space_to_non_fill = readDistance(tree, "space_to_non_fill", dbu);
  if (tree.get_child_optional("space_line_end")) {
    config.space_line_end = readDistance(tree, "space_line_end", dbu);
  }
  const auto& widths = tree.get_child("width");
  const auto& heights = tree.get_child("height");
  if (widths.empty() || widths.size() != heights.size()) {
    throw std::invalid_argument(
        "Fill width/height lists must be nonempty and equal in length.");
  }
  auto height = heights.begin();
  for (const auto& [unused, width] : widths) {
    const int w = readDistance(width, "", dbu);
    const int h = readDistance((height++)->second, "", dbu);
    if (w == 0 || h == 0) {
      throw std::invalid_argument("Fill dimensions must be positive in DBU.");
    }
    config.shapes.emplace_back(w, h);
  }
  return config;
}

void readStagger(const pt::ptree& tree, const int dbu, FillShapesConfig& config)
{
  const auto mode = tree.get<std::string>("stagger", "OFF");
  if (mode != "ON" && mode != "OFF") {
    throw std::invalid_argument("stagger must be ON or OFF.");
  }
  config.stagger = mode == "ON";
  const auto offset = tree.get_child_optional("stagger_offset");
  if (!offset) {
    if (config.stagger) {
      throw std::invalid_argument(
          "stagger ON requires stagger_offset: [x, y] in microns.");
    }
    return;
  }
  if (offset->size() != 2 || !offset->data().empty()) {
    throw std::invalid_argument(
        "stagger_offset must be an array of two distances in microns.");
  }
  std::array<int, 2> values{};
  std::array<bool, 2> nonzero{};
  size_t index = 0;
  for (const auto& [name, value] : *offset) {
    if (!name.empty() || !value.empty()) {
      throw std::invalid_argument(
          "stagger_offset must contain two numeric distances.");
    }
    const double offset_um = value.get_value<double>();
    nonzero[index] = offset_um != 0.0;
    const double distance = std::round(offset_um * dbu);
    if (!std::isfinite(distance) || distance < std::numeric_limits<int>::min()
        || distance > std::numeric_limits<int>::max()) {
      throw std::invalid_argument(
          "stagger_offset must be finite and representable in DBU.");
    }
    values[index++] = static_cast<int>(distance);
  }
  if (nonzero[0] && nonzero[1]) {
    throw std::invalid_argument(
        "stagger_offset must select only one axis: [x, 0] or [0, y].");
  }
  config.stagger_offset = odb::Point(values[0], values[1]);
}

FillLayerConfig readLayer(const pt::ptree& tree, const int dbu)
{
  FillLayerConfig config;
  config.space_to_outline = readDistance(tree, "space_to_outline", dbu);
  const auto& non_opc = tree.get_child("non-opc");
  config.non_opc = readShapes(non_opc, dbu);
  config.non_opc.space_line_end = 0;
  readStagger(non_opc, dbu, config.non_opc);
  config.num_masks = non_opc.get_child("datatype").size();
  const auto opc = tree.get_child_optional("opc");
  config.has_opc = opc.has_value();
  if (opc) {
    config.opc = readShapes(*opc, dbu);
    config.opc_halo = readDistance(*opc, "halo", dbu);
  }
  return config;
}

// Retain the fill algorithm's shape-size ordering and polygon-local grid.
void appendShapeCandidates(Polygon90Set& remaining,
                           dbTechLayer* layer,
                           const FillShapesConfig& config,
                           int width,
                           int height,
                           const bool has_next_size,
                           std::vector<Rectangle>& candidates)
{
  if ((layer->getDirection() == dbTechLayerDir::HORIZONTAL && width < height)
      || (layer->getDirection() != dbTechLayerDir::HORIZONTAL
          && height < width)) {
    std::swap(width, height);
  }
  const auto [space_x, space_y] = getSpacing(layer, config);
  Polygon90Set available = remaining;
  const int ew = std::max(width / 2 - 1, 0);
  const int ns = std::max(height / 2 - 1, 0);
  shrink(available, ew, ew, ns, ns);
  bloat(available, ew, ew, ns, ns);
  pruneFillArea(available, layer, config);
  std::vector<Polygon90> polygons;
  available.get(polygons);
  Polygon90Set occupied;
  for (const auto& polygon : polygons) {
    Rectangle bounds;
    extents(bounds, polygon);
    const Polygon90Set lattice
        = makeFillPattern(bounds, width, height, space_x, space_y, config);
    Polygon90Set fills = lattice & polygon;
    const int64_t area = int64_t{width} * height;
    keep(fills, area, area, width - 1, width, height - 1, height);
    fills.get_rectangles(candidates);
    if (has_next_size) {
      occupied += bloat(fills, space_x, space_x, space_y, space_y);
    }
  }
  if (has_next_size) {
    remaining -= occupied;
  }
}
}  // namespace

FillLayerConfigs loadFillRules(const char* filename,
                               dbTech* tech,
                               utl::Logger* logger)
{
  pt::ptree tree;
  pt::read_json(filename, tree);
  FillLayerConfigs result;
  const int dbu = tech->getDbUnitsPerMicron();
  for (const auto& [unused, layer] : tree.get_child("layers")) {
    const FillLayerConfig config = readLayer(layer, dbu);
    const auto names = layer.get_child_optional("names");
    if (names) {
      for (const auto& [unused_name, name] : *names) {
        auto* tech_layer = tech->findLayer(name.data().c_str());
        if (tech_layer == nullptr) {
          logger->error(
              FIN, 1, "Layer {} in names was not found.", name.data());
        }
        result[tech_layer] = config;
      }
    } else {
      const auto name = layer.get<std::string>("name");
      auto* tech_layer = tech->findLayer(name.c_str());
      if (tech_layer == nullptr) {
        logger->error(FIN, 2, "Layer {} not found.", name);
      }
      result[tech_layer] = config;
    }
  }
  return result;
}

std::vector<Rectangle> makeFillCandidates(const Polygon90Set& area,
                                          dbTechLayer* layer,
                                          const FillShapesConfig& config)
{
  std::vector<Polygon90> polygons;
  area.get(polygons);
  std::vector<Rectangle> candidates;
  for (const auto& polygon : polygons) {
    Polygon90Set remaining;
    remaining.insert(polygon);
    for (size_t i = 0; i < config.shapes.size(); ++i) {
      const auto [width, height] = config.shapes[i];
      appendShapeCandidates(remaining,
                            layer,
                            config,
                            width,
                            height,
                            i + 1 < config.shapes.size(),
                            candidates);
    }
  }
  return candidates;
}

double rectangleArea(const Rectangle& rectangle)
{
  return (static_cast<double>(xh(rectangle)) - xl(rectangle))
         * (static_cast<double>(yh(rectangle)) - yl(rectangle));
}
// Candidates already satisfy spacing; selection only enforces the LP budget.
std::vector<Rectangle> selectFillCandidates(
    const std::vector<Rectangle>& candidates,
    const double target_area)
{
  if (!std::isfinite(target_area) || target_area < 0) {
    throw std::invalid_argument("Invalid fill selection budget.");
  }
  std::vector<Rectangle> selected;
  double placed = 0.0;
  for (const auto& candidate : candidates) {
    const double area = rectangleArea(candidate);
    if (placed + area <= target_area) {
      selected.push_back(candidate);
      placed += area;
    }
  }
  return selected;
}

void forEachFillTile(const size_t count,
                     const int num_threads,
                     const std::function<void(size_t)>& task)
{
  if (num_threads < 1) {
    throw std::invalid_argument("Fill thread count must be positive.");
  }
  std::exception_ptr failure;
#pragma omp parallel for num_threads(num_threads) schedule(dynamic)
  for (size_t i = 0; i < count; ++i) {
    try {
      task(i);
    } catch (...) {
#pragma omp critical(fin_fill_failure)
      {
        if (!failure) {
          failure = std::current_exception();
        }
      }
    }
  }
  if (failure) {
    std::rethrow_exception(failure);
  }
}
}  // namespace fin

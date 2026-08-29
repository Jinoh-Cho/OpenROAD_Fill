// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillGeometry.h"

#include <array>
#include <vector>

#include "odb/dbShape.h"
#include "odb/dbTypes.h"

namespace fin {

Polygon90 makeRect(int x_lo, int y_lo, int x_hi, int y_hi)
{
  using Point = Polygon90::point_type;
  const std::array<Point, 4> points = {Point(x_lo, y_lo),
                                       Point(x_hi, y_lo),
                                       Point(x_hi, y_hi),
                                       Point(x_lo, y_hi)};
  Polygon90 polygon;
  polygon.set(points.begin(), points.end());
  return polygon;
}

void insertShape(const odb::dbShape& shape,
                 Polygon90Set& polygon_set,
                 odb::dbTechLayer* layer)
{
  switch (shape.getType()) {
    case odb::dbShape::VIA:
    case odb::dbShape::TECH_VIA: {
      odb::dbTechLayer* top_layer;
      odb::dbTechLayer* bottom_layer;
      if (shape.getType() == odb::dbShape::VIA) {
        auto* via = shape.getVia();
        top_layer = via->getTopLayer();
        bottom_layer = via->getBottomLayer();
      } else {
        auto* via = shape.getTechVia();
        top_layer = via->getTopLayer();
        bottom_layer = via->getBottomLayer();
      }
      if (top_layer != layer && bottom_layer != layer) {
        return;
      }
      std::vector<odb::dbShape> boxes;
      odb::dbShape::getViaBoxes(shape, boxes);
      for (const auto& box : boxes) {
        if (box.getTechLayer() == layer) {
          polygon_set.insert(
              makeRect(box.xMin(), box.yMin(), box.xMax(), box.yMax()));
        }
      }
      break;
    }
    case odb::dbShape::SEGMENT:
    case odb::dbShape::TECH_VIA_BOX:
    case odb::dbShape::VIA_BOX:
      if (shape.getTechLayer() == layer) {
        polygon_set.insert(
            makeRect(shape.xMin(), shape.yMin(), shape.xMax(), shape.yMax()));
      }
      break;
  }
}

Polygon90Set orNonFills(odb::dbBlock* block, odb::dbTechLayer* layer)
{
  Polygon90Set non_fill;
  odb::dbShape shape;
  odb::dbWireShapeItr wire_shapes;
  for (auto* net : block->getNets()) {
    auto* wire = net->getWire();
    if (wire == nullptr) {
      continue;
    }
    for (wire_shapes.begin(wire); wire_shapes.next(shape);) {
      insertShape(shape, non_fill, layer);
    }
  }
  std::vector<odb::dbShape> via_shapes;
  for (auto* net : block->getNets()) {
    for (auto* swire : net->getSWires()) {
      for (auto* sbox : swire->getWires()) {
        if (sbox->isVia()) {
          shape.setVia(sbox->getBlockVia(), sbox->getBox());
          odb::dbShape::getViaBoxes(shape, via_shapes);
          for (const auto& via_shape : via_shapes) {
            insertShape(via_shape, non_fill, layer);
          }
        } else if (sbox->getTechLayer() == layer) {
          non_fill.insert(
              makeRect(sbox->xMin(), sbox->yMin(), sbox->xMax(), sbox->yMax()));
        }
      }
    }
  }
  odb::dbInstShapeItr instance_shapes(false);
  for (auto* instance : block->getInsts()) {
    for (instance_shapes.begin(instance, odb::dbInstShapeItr::ALL);
         instance_shapes.next(shape);) {
      insertShape(shape, non_fill, layer);
    }
  }
  return non_fill;
}

}  // namespace fin

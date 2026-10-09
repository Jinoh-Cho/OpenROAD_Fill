// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <utility>
#include <vector>

#include "FillConfig.h"
#include "polygon.h"

namespace fin {
class Graphics;

// Geometry and placement helpers; no LP formulation or solver dispatch.
namespace lpfill {
// Resolve fill spacing and line-end spacing along the layer's direction.
std::pair<int, int> getSpacing(odb::dbTechLayer* layer,
                               const FillShapesConfig& config);
// Remove regions that can cause spacing violations between separate polygons.
void prune(Polygon90Set& fill_area,
           odb::dbTechLayer* layer,
           const FillShapesConfig& config,
           Graphics* graphics);

// Generate legal rectangular candidates within an already prepared polygon.
std::vector<Rectangle> makeFillCandidates(const Polygon90& area,
                                          odb::dbTechLayer* layer,
                                          const FillShapesConfig& config,
                                          Graphics* graphics);
// Prepare a tile's legal region and collect candidates from its polygons.
std::vector<Rectangle> makeTileFillCandidates(const odb::Rect& tile,
                                              const Polygon90Set& non_fill,
                                              odb::dbTechLayer* layer,
                                              const FillShapesConfig& config,
                                              Graphics* graphics,
                                              Polygon90Set* fillable_area
                                              = nullptr);
// Exclude obstacles and reserve boundary spacing between adjacent tiles.
Polygon90Set makeTileFillArea(const odb::Rect& tile,
                              const Polygon90Set& non_fill,
                              odb::dbTechLayer* layer,
                              const FillShapesConfig& config,
                              Graphics* graphics);

}  // namespace lpfill
}  // namespace fin

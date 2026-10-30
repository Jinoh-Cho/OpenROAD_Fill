// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "FillConfig.h"
#include "polygon.h"

namespace fin {
class Graphics;
class TileGrid;

// Geometry and placement helpers; no LP formulation or solver dispatch.
namespace lpfill {
// Opt-in, immediately flushed diagnostics for candidate geometry operations.
bool candidateTraceEnabled();
void traceCandidate(const std::string& message);

struct LayerFillAreas
{
  std::vector<Polygon90Set> tile_areas;
  std::vector<double> bloated_non_fill_areas;
  int threads = 1;
  // Disjoint elapsed phases of free-area preparation.
  std::map<std::string, double> phase_seconds;
};

// Subtract expanded obstacles once, then distribute free rectangles to tiles.
// Boundary spacing and pruning run in separate parallel tile phases.
// Graphics and detailed tracing force serial preparation.
LayerFillAreas makeLayerFillAreas(const TileGrid& grid,
                                  const Polygon90Set& bloated_non_fill,
                                  odb::dbTechLayer* layer,
                                  const FillShapesConfig& config,
                                  Graphics* graphics,
                                  int threads = 1);

struct LayerFillCandidates
{
  std::vector<std::vector<Rectangle>> tiles;
  double wall_seconds = 0.0;
  double work_seconds = 0.0;
  int threads = 1;
};

// Independent tile tasks retain their original order. Graphics and detailed
// tracing use the serial path. Worker exceptions are rethrown after joining.
LayerFillCandidates makeLayerFillCandidates(
    const std::vector<Polygon90Set>& tile_areas,
    odb::dbTechLayer* layer,
    const FillShapesConfig& config,
    Graphics* graphics,
    int threads);

struct LayerFillPlacement
{
  std::vector<Rectangle> fills;
  std::vector<double> placed_areas;
  Polygon90Set selected_shapes;
  double wall_seconds = 0.0;
  double work_seconds = 0.0;
  int threads = 1;
  double select_seconds = 0.0;
  double select_work_seconds = 0.0;
  // Disjoint elapsed phases; selection/shape work sums are kept separately.
  std::map<std::string, double> phase_seconds;
};

// Select candidates and build independent tile geometry in parallel. Merge in
// original order; the caller inserts fills into OpenDB sequentially afterward.
LayerFillPlacement selectLayerFillCandidates(
    const std::vector<std::vector<Rectangle>>& candidates,
    const std::vector<double>& target_areas,
    int threads);

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
std::vector<Rectangle> makeTileFillCandidates(const Polygon90Set& tile_area,
                                              odb::dbTechLayer* layer,
                                              const FillShapesConfig& config,
                                              Graphics* graphics);

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

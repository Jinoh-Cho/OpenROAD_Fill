// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "odb/PtrSetMap.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "polygon.h"
#include "utl/Logger.h"

namespace fin {

struct FillShapesConfig
{
  std::vector<std::pair<int, int>> shapes;
  int space_to_fill = 0;
  int space_to_non_fill = 0;
  int space_line_end = 0;
  bool stagger = false;
  odb::Point stagger_offset{0, 0};
  odb::Point pattern_origin{0, 0};
};

struct FillLayerConfig
{
  int space_to_outline = 0;
  int num_masks = 0;
  int opc_halo = 0;
  bool has_opc = false;
  FillShapesConfig opc;
  FillShapesConfig non_opc;
};

using FillLayerConfigs = odb::PtrMap<odb::dbTechLayer, FillLayerConfig>;

FillLayerConfigs loadFillRules(const char* filename,
                               odb::dbTech* tech,
                               utl::Logger* logger);
Polygon90 makeRect(int x_lo, int y_lo, int x_hi, int y_hi);
Polygon90Set collectNonFillShapes(odb::dbBlock* block,
                                  odb::dbTechLayer* layer,
                                  bool include_fill_obstructions = true);
std::pair<int, int> getSpacing(odb::dbTechLayer* layer,
                               const FillShapesConfig& config);
void pruneFillArea(Polygon90Set& area,
                   odb::dbTechLayer* layer,
                   const FillShapesConfig& config);
Polygon90Set makeFillPattern(const Rectangle& bounds,
                             int width,
                             int height,
                             int space_x,
                             int space_y,
                             const FillShapesConfig& config);
std::vector<Rectangle> makeFillCandidates(const Polygon90Set& area,
                                          odb::dbTechLayer* layer,
                                          const FillShapesConfig& config);
std::vector<Rectangle> selectFillCandidates(
    const std::vector<Rectangle>& candidates,
    double target_area);

double rectangleArea(const Rectangle& rectangle);
void forEachFillTile(size_t count,
                     int num_threads,
                     const std::function<void(size_t)>& task);

}

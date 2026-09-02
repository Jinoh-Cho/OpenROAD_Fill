// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <memory>
#include <string>
#include <utility>

#include "FillConfig.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "polygon.h"
#include "utl/Logger.h"

namespace fin {

class Graphics;

// Experimental utilities that support the minimum-variation fill algorithm.
// Keeping them separate from DensityFill lets the established density-fill
// implementation remain independent of algorithm exploration.
class MinVarFill
{
 public:
  MinVarFill(odb::dbDatabase* db, utl::Logger* logger, bool debug = false);
  ~MinVarFill();

  // Entry point for the minimum-variation fill algorithm.
  void fill(const char* rules_filename, const odb::Rect& fill_area);
  void benchmarkRectangleExtraction(const char* rules_filename,
                                    const odb::Rect& fill_area,
                                    int left_copies,
                                    int right_copies,
                                    int bottom_copies,
                                    int top_copies,
                                    int runs);
  double tileGridMetalArea(const char* rules_filename,
                           const odb::Rect& region,
                           const odb::Point& origin,
                           int window_size,
                           int resolution,
                           const char* svg_filename);
  double fixedDissectionLp(const char* rules_filename,
                           const odb::Rect& region,
                           const odb::Point& origin,
                           int window_size,
                           int resolution,
                           double max_density,
                           const char* svg_filename);

  static bool writeFillAreaSvg(const std::string& filename,
                               const Polygon90Set& fill_area,
                               const Polygon90Set& non_fill,
                               const odb::Rect& bounds);

 private:
  static std::pair<int, int> getSpacing(odb::dbTechLayer* layer,
                                        const FillShapesConfig& config);
  static void prune(Polygon90Set& fill_area,
                    odb::dbTechLayer* layer,
                    const FillShapesConfig& config,
                    Graphics* graphics);
  static void fillPolygon(const Polygon90& area,
                          odb::dbTechLayer* layer,
                          odb::dbBlock* block,
                          const FillShapesConfig& config,
                          int num_masks,
                          bool needs_opc,
                          Graphics* graphics,
                          Polygon90Set* filled_area = nullptr);
  void fillLayer(odb::dbBlock* block,
                 odb::dbTechLayer* layer,
                 const odb::Rect& fill_bounds);

  odb::dbDatabase* db_;
  utl::Logger* logger_;
  FillLayerConfigs layers_;
  std::unique_ptr<Graphics> graphics_;
};

}  // namespace fin

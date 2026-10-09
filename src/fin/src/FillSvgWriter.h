// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>

#include "TileGrid.h"

namespace fin {
bool writeTileGridSvg(
    const TileGrid& grid,
    const std::string& filename,
    const boost::polygon::polygon_90_set_data<int>& metal_shapes,
    int dbu_per_micron,
    const std::vector<double>* planned_fill_areas = nullptr,
    bool show_tile_values = true,
    const boost::polygon::polygon_90_set_data<int>* placed_fill_shapes
    = nullptr,
    const std::vector<Polygon90>* fillable_polygons = nullptr,
    const std::vector<double>* target_tile_densities = nullptr,
    const std::vector<TileViolation>* tile_violations = nullptr,
    const std::vector<WindowViolation>* window_violations = nullptr,
    const std::vector<double>* bloated_non_fill_areas = nullptr,
    const std::vector<double>* fillable_region_areas = nullptr);

bool writeFillAreaSvg(const std::string& filename,
                      const Polygon90Set& fill_area,
                      const Polygon90Set& non_fill,
                      const odb::Rect& bounds);

}  // namespace fin

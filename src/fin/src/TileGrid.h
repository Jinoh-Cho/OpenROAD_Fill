// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "DensityAnalysis.h"
#include "polygon.h"

namespace fin {
struct TileGridConfig
{
  odb::Rect region;
  odb::Point origin;
  int window_size;
  int resolution;
};

class TileGrid
{
 public:
  explicit TileGrid(const TileGridConfig& config);
  const std::vector<odb::Rect>& tiles() const { return tiles_; }
  const odb::Rect& region() const { return region_; }
  size_t tileColumns() const { return tile_columns_; }
  std::vector<DensityWindow>& windows() { return windows_; }
  const std::vector<DensityWindow>& windows() const { return windows_; }
  const std::vector<double>& metalAreas() const { return metal_areas_; }
  std::vector<std::vector<size_t>> windowTileIndices() const;
  std::vector<std::vector<size_t>> windowTileIndices(
      const std::vector<size_t>& window_indices) const;
  // Calculate the metal area and density of every tile and density window.
  // The polygon set is a union, so overlapping shapes are counted once.
  void calculateMetalDensities(
      const boost::polygon::polygon_90_set_data<int>& metal_shapes);
  const std::vector<double>& metalDensities() const { return metal_densities_; }
  double totalMetalArea() const;

 private:
  void calculateWindowDensities();

  std::vector<odb::Rect> tiles_;
  odb::Rect region_;
  std::vector<DensityWindow> windows_;
  size_t tile_columns_ = 0;
  int tiles_per_window_ = 0;
  std::vector<double> metal_densities_;
  std::vector<double> metal_areas_;
};

}  // namespace fin

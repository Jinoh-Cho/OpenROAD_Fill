// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstddef>
#include <vector>

#include "odb/geom.h"
#include "polygon.h"

namespace fin {

struct FillWindow
{
  odb::Rect bounds;
  std::vector<size_t> tiles;
};

// Window boundaries partition the core exactly, including nonuniform edge
// tiles and window dimensions that are not integer multiples of the step.
class FillGrid
{
 public:
  FillGrid(const odb::Rect& region,
           const odb::Point& window_size,
           const odb::Point& window_step);
  const std::vector<odb::Rect>& tiles() const;
  const std::vector<FillWindow>& windows() const;
  std::vector<Polygon90Set> partition(
      const std::vector<Rectangle>& rectangles) const;
  std::vector<double> metalAreas(const std::vector<Rectangle>& rectangles,
                                 int num_threads) const;
  std::vector<double> windowDensities(
      const std::vector<double>& tile_metal_areas) const;

 private:
  std::vector<int> x_edges_;
  std::vector<int> y_edges_;
  std::vector<odb::Rect> tiles_;
  std::vector<FillWindow> windows_;
};

struct FillDensitySummary
{
  double minimum = 0.0;
  double maximum = 0.0;
  double mean = 0.0;
  size_t below_minimum = 0;
  size_t above_maximum = 0;
};

FillDensitySummary summarizeFillDensity(const std::vector<double>& densities,
                                        double minimum,
                                        double maximum);

}  // namespace fin

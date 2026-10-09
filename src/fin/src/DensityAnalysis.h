// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "odb/geom.h"

namespace fin {
struct DensityWindow
{
  odb::Rect bounds;
  size_t first_tile_x;
  size_t first_tile_y;
  double metal_area = 0.0;
  double density = 0.0;
  double planned_fill_area = 0.0;
  double post_fill_density = 0.0;
};

// Exact minimum and maximum density windows over every placement of a square
// window inside a region.
struct FloatingDensityResult
{
  odb::Rect min_window;
  odb::Rect max_window;
  double min_density = 0.0;
  double max_density = 0.0;
  struct Profile
  {
    int y = 0;
    std::vector<std::pair<int, double>> points;
  };
  std::vector<Profile> profiles;
};

enum class TileViolationReason
{
  kCapacity,
  kDiscreteCandidate
};

struct TileViolation
{
  size_t tile_index;
  TileViolationReason reason;
  double required_density;
  double available_density;
};

struct WindowViolation
{
  size_t window_index;
  double required_fill_area;
  double fill_budget;
};

struct WindowDensitySummary
{
  double min_density = 0.0;
  double max_density = 0.0;
  double mean_density = 0.0;
  double variance = 0.0;
  size_t min_violation_count = 0;
  size_t max_violation_count = 0;
};

constexpr size_t WindowDensityHistogramBins = 20;
using WindowDensityHistogram = std::array<size_t, WindowDensityHistogramBins>;

}  // namespace fin

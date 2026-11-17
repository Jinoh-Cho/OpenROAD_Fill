// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once

#include <cstdint>
#include <utility>

#include "odb/geom.h"
#include "polygon.h"

namespace fin {
enum class FillPattern
{
  Grid,
  Stagger
};

struct FillPatternConfig
{
  FillPattern type = FillPattern::Grid;
  // Fraction of horizontal pitch; required in JSON for stagger (0 <= x < 1).
  double stagger_offset = 0.0;
  odb::Point origin{0, 0};
};

// Grid preserves the legacy polygon-local phase. Stagger uses a common origin.
void insertPatternRectangles(Polygon90Set& output,
                             const Rectangle& bounds,
                             int width,
                             int height,
                             int space_x,
                             int space_y,
                             const FillPatternConfig& pattern);

// Undo alternating-row displacement for uniform's logical bitmap adjacency.
std::int64_t unshiftPatternX(std::int64_t x,
                             std::int64_t y,
                             std::int64_t pitch_x,
                             std::int64_t pitch_y,
                             const FillPatternConfig& pattern);
}  // namespace fin

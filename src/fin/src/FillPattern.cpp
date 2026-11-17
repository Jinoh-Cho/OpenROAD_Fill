// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "FillPattern.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "FillGeometry.h"

namespace fin {
namespace {
using Coordinate = std::int64_t;
Coordinate floorDivide(Coordinate value, Coordinate divisor)
{
  const auto quotient = value / divisor;
  return quotient - (value % divisor < 0 ? 1 : 0);
}

Coordinate ceilDivide(Coordinate value, Coordinate divisor)
{
  return -floorDivide(-value, divisor);
}

Coordinate rowShift(Coordinate row,
                    Coordinate pitch_x,
                    const FillPatternConfig& pattern)
{
  if (!std::isfinite(pattern.stagger_offset) || pattern.stagger_offset < 0
      || pattern.stagger_offset >= 1) {
    throw std::invalid_argument(
        "stagger_offset must be a finite pitch fraction in [0, 1).");
  }
  const auto shift
      = static_cast<Coordinate>(std::llround(pitch_x * pattern.stagger_offset));
  return row % 2 != 0 ? shift % pitch_x : 0;
}
}  // namespace

void insertPatternRectangles(Polygon90Set& output,
                             const Rectangle& bounds,
                             int width,
                             int height,
                             int space_x,
                             int space_y,
                             const FillPatternConfig& pattern)
{
  const Coordinate pitch_x = Coordinate(width) + space_x;
  const Coordinate pitch_y = Coordinate(height) + space_y;
  if (width <= 0 || height <= 0 || pitch_x <= 0 || pitch_y <= 0) {
    throw std::invalid_argument(
        "Fill pattern requires positive sizes/pitches.");
  }
  if (pattern.type == FillPattern::Grid) {
    // Retain even partial boundary rectangles: legacy union/intersection/keep
    // behavior can depend on them when fill spacing is zero.
    for (Coordinate x = xl(bounds); x < xh(bounds); x += pitch_x) {
      for (Coordinate y = yl(bounds); y < yh(bounds); y += pitch_y) {
        if (x + width > std::numeric_limits<int>::max()
            || y + height > std::numeric_limits<int>::max()) {
          continue;
        }
        output.insert(makeRect(x, y, x + width, y + height));
      }
    }
    return;
  }
  const Coordinate first_row
      = ceilDivide(Coordinate(yl(bounds)) - pattern.origin.y(), pitch_y);
  for (Coordinate row = first_row;; ++row) {
    const Coordinate y = pattern.origin.y() + row * pitch_y;
    if (y + height > yh(bounds)) {
      break;
    }
    const Coordinate x_origin
        = pattern.origin.x() + rowShift(row, pitch_x, pattern);
    const Coordinate first_col
        = ceilDivide(Coordinate(xl(bounds)) - x_origin, pitch_x);
    for (Coordinate x = x_origin + first_col * pitch_x; x + width <= xh(bounds);
         x += pitch_x) {
      output.insert(makeRect(x, y, x + width, y + height));
    }
  }
}

std::int64_t unshiftPatternX(std::int64_t x,
                             std::int64_t y,
                             std::int64_t pitch_x,
                             std::int64_t pitch_y,
                             const FillPatternConfig& pattern)
{
  if (pattern.type == FillPattern::Grid) {
    return x;
  }
  const Coordinate row = floorDivide(y - pattern.origin.y(), pitch_y);
  return x - rowShift(row, pitch_x, pattern);
}
}  // namespace fin

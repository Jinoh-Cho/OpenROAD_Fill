// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "FillUtill.h"
#include "gtest/gtest.h"

namespace fin {
namespace {

double windowDensity(const std::vector<odb::Rect>& rectangles,
                     const odb::Rect& window)
{
  double area = 0.0;
  for (const odb::Rect& rectangle : rectangles) {
    const int overlap_x
        = std::max(0,
                   std::min(rectangle.xMax(), window.xMax())
                       - std::max(rectangle.xMin(), window.xMin()));
    const int overlap_y
        = std::max(0,
                   std::min(rectangle.yMax(), window.yMax())
                       - std::max(rectangle.yMin(), window.yMin()));
    area += static_cast<double>(overlap_x) * overlap_y;
  }
  return area / window.area();
}

TEST(FillUtillTest, Alg2MatchesBruteForceExtrema)
{
  const odb::Rect region(0, 0, 10, 10);
  const int window_size = 4;
  const std::vector<odb::Rect> rectangles{
      odb::Rect(0, 0, 2, 7), odb::Rect(2, 5, 6, 9), odb::Rect(7, 1, 10, 4)};

  double expected_min = 1.0;
  double expected_max = 0.0;
  for (int y = region.yMin(); y + window_size <= region.yMax(); y++) {
    for (int x = region.xMin(); x + window_size <= region.xMax(); x++) {
      const double density = windowDensity(
          rectangles, odb::Rect(x, y, x + window_size, y + window_size));
      expected_min = std::min(expected_min, density);
      expected_max = std::max(expected_max, density);
    }
  }

  const FloatingDensityResult result
      = analyzeFloatingDensityAlg2(rectangles, region, window_size);
  const FloatingDensityResult alg3_result
      = analyzeFloatingDensityAlg3(rectangles, region, window_size);

  EXPECT_NEAR(result.min_density, expected_min, 1e-9);
  EXPECT_NEAR(result.max_density, expected_max, 1e-9);
  EXPECT_NEAR(windowDensity(rectangles, result.min_window), expected_min, 1e-9);
  EXPECT_NEAR(windowDensity(rectangles, result.max_window), expected_max, 1e-9);
  EXPECT_NEAR(alg3_result.min_density, result.min_density, 1e-9);
  EXPECT_NEAR(alg3_result.max_density, result.max_density, 1e-9);
  EXPECT_NEAR(
      windowDensity(rectangles, alg3_result.min_window), expected_min, 1e-9);
  EXPECT_NEAR(
      windowDensity(rectangles, alg3_result.max_window), expected_max, 1e-9);
  EXPECT_LE(result.profiles.size(), 50);
  EXPECT_FALSE(result.profiles.empty());
  for (const FloatingDensityResult::Profile& profile : result.profiles) {
    EXPECT_FALSE(profile.points.empty());
    EXPECT_EQ(profile.points.front().first, region.xMin());
    EXPECT_EQ(profile.points.back().first, region.xMax() - window_size);
  }
  for (const FloatingDensityResult::Profile& profile : alg3_result.profiles) {
    ASSERT_FALSE(profile.points.empty());
    EXPECT_EQ(profile.points.front().first, region.xMin());
    EXPECT_EQ(profile.points.back().first, region.xMax() - window_size);
  }
}

TEST(FillUtillTest, Alg2RejectsWindowLargerThanRegion)
{
  EXPECT_THROW(analyzeFloatingDensityAlg2({}, odb::Rect(0, 0, 10, 10), 11),
               std::invalid_argument);
}

TEST(FillUtillTest, Alg3RejectsWindowLargerThanRegion)
{
  EXPECT_THROW(analyzeFloatingDensityAlg3({}, odb::Rect(0, 0, 10, 10), 11),
               std::invalid_argument);
}

}  // namespace
}  // namespace fin

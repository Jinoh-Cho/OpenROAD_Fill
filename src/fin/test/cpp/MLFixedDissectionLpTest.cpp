// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <stdexcept>

#include "MLFixedDissectionLp.h"
#include "gtest/gtest.h"

namespace fin {
namespace {

TEST(MLFixedDissectionLpTest, SolvesRecordedMixedResolutionWindows)
{
  MLFixedDissectionLpProblem problem{
      .max_density = 0.75,
      .tile_areas = {4.0, 4.0, 8.0},
      .feature_areas = {1.0, 1.0, 2.0},
      .max_fill_areas = {3.0, 3.0, 6.0},
      .windows = {{0, 1}, {2}},
  };

  const MLFixedDissectionLpResult result = solveMLFixedDissectionLp(problem);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 3);
  EXPECT_NEAR(result.min_window_area, 6.0, 1e-6);
  EXPECT_NEAR(result.fill_areas[0] + result.fill_areas[1], 4.0, 1e-6);
  EXPECT_NEAR(result.fill_areas[2], 4.0, 1e-6);
}

TEST(MLFixedDissectionLpTest, RejectsInvalidWindowTile)
{
  MLFixedDissectionLpProblem problem{
      .max_density = 0.75,
      .tile_areas = {4.0},
      .feature_areas = {1.0},
      .max_fill_areas = {3.0},
      .windows = {{1}},
  };

  EXPECT_THROW(solveMLFixedDissectionLp(problem), std::invalid_argument);
}

}  // namespace
}  // namespace fin

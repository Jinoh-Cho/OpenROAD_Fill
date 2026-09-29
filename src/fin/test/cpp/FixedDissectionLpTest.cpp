// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <stdexcept>

#include "FixedDissectionLp.h"
#include "gtest/gtest.h"

namespace fin {
namespace {

TEST(FixedDissectionLpTest, EnforcesMinimumWindowDensity)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.5,
      .min_window_density = 0.4,
      .tile_areas = {10.0},
      .feature_areas = {0.0},
      .max_fill_areas = {3.0},
      .windows = {{0}},
  };

  EXPECT_FALSE(solveFixedDissectionLp(problem).solved);
}

TEST(FixedDissectionLpTest, RejectsInvalidMinimumWindowDensity)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.5,
      .min_window_density = 1.1,
      .tile_areas = {10.0},
      .feature_areas = {0.0},
      .max_fill_areas = {10.0},
      .windows = {{0}},
  };

  EXPECT_THROW(solveFixedDissectionLp(problem), std::invalid_argument);
}

TEST(FixedDissectionLpTest, EnforcesMaximumTileDensity)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.9,
      .max_tile_density = 0.3,
      .tile_areas = {10.0},
      .feature_areas = {0.0},
      .max_fill_areas = {10.0},
      .windows = {{0}},
  };

  const FixedDissectionLpResult result = solveFixedDissectionLp(problem);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 1);
  EXPECT_NEAR(result.fill_areas[0], 3.0, 1e-6);
}

}  // namespace
}  // namespace fin

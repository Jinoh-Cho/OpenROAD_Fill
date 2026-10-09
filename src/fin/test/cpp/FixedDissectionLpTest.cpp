// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <stdexcept>

#include "FixedDissectionLp.h"
#include "LipLpFill.h"
#include "MinFillAmountLp.h"
#include "MinVarLP.h"
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

  EXPECT_FALSE(solveMinVarLP(problem).solved);
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

  EXPECT_THROW(solveMinVarLP(problem), std::invalid_argument);
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

  const FixedDissectionLpResult result = solveMinVarLP(problem);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 1);
  EXPECT_NEAR(result.fill_areas[0], 3.0, 1e-6);
}

TEST(FixedDissectionLpTest, MinFillAmountUsesNamedPlanner)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.8,
      .min_window_density = 0.3,
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 0.0},
      .max_fill_areas = {10.0, 10.0},
      .windows = {{0}, {1}},
  };

  const FixedDissectionLpResult result = solveMinFillAmountLp(problem);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(result.fill_areas[0], 3.0, 1e-6);
  EXPECT_NEAR(result.fill_areas[1], 3.0, 1e-6);

  const FixedDissectionLpResult min_var = solveMinVarLP(problem);
  ASSERT_TRUE(min_var.solved);
  EXPECT_NEAR(min_var.min_window_area, 8.0, 1e-6);
  EXPECT_NEAR(min_var.fill_areas[0], 8.0, 1e-6);
  EXPECT_NEAR(min_var.fill_areas[1], 8.0, 1e-6);
}

TEST(FixedDissectionLpTest, MinimizesLipDensityRange)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.8,
      .min_tile_density = 0.0,
      .max_tile_density = 1.0,
      .min_window_density = 0.3,
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 8.0},
      .max_fill_areas = {10.0, 2.0},
      .windows = {{0}, {1}},
  };

  const FixedDissectionLpResult result = solveLipLpFill(problem, {{0, 1}});

  ASSERT_TRUE(result.solved);
  EXPECT_NEAR(result.lip_value, 0.0, 1e-6);
  const double first_density = result.fill_areas[0] / 10.0;
  const double second_density = (8.0 + result.fill_areas[1]) / 10.0;
  EXPECT_NEAR(first_density, second_density, 1e-6);
}

}  // namespace
}  // namespace fin

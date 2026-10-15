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

class UnattainableTileMinimumTest : public testing::TestWithParam<int>
{
 protected:
  FixedDissectionLpResult solve(const FixedDissectionLpProblem& problem)
  {
    if (GetParam() == 0) {
      return solveMinVarLP(problem);
    }
    if (GetParam() == 1) {
      return solveMinFillAmountLp(problem);
    }
    return solveLipLpFill(problem, {{0, 1}});
  }

  FixedDissectionLpProblem problem_{
      .max_window_density = 0.6,
      .min_tile_density = 0.2,
      .tile_areas = {10.0, 10.0, 10.0},
      .feature_areas = {0.0, 0.0, 0.0},
      .max_fill_areas = {0.0, 10.0, 1.0},
      .windows = {{0, 1}, {1, 2}},
  };
};

TEST_P(UnattainableTileMinimumTest, OmitsOnlyUnattainableTileMinimums)
{
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 3);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-6);
  EXPECT_GE(result.fill_areas[1], 2.0 - 1e-6);
  EXPECT_LE(result.fill_areas[2], 1.0 + 1e-6);
  for (const auto& window : problem_.windows) {
    EXPECT_LE(result.fill_areas[window[0]] + result.fill_areas[window[1]],
              12.0 + 1e-6);
  }
}

TEST_P(UnattainableTileMinimumTest, RetainsWindowMinimums)
{
  problem_.min_window_density = 0.15;
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_GE(result.fill_areas[1], 3.0 - 1e-6);

  problem_.max_fill_areas[1] = 1.0;
  EXPECT_FALSE(solve(problem_).solved);
}

TEST_P(UnattainableTileMinimumTest, RetainsMaximumTileDensity)
{
  problem_.min_tile_density = 0.05;
  problem_.max_tile_density = 0.1;
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_LE(result.fill_areas[1], 1.0 + 1e-6);
  EXPECT_LE(result.fill_areas[2], 1.0 + 1e-6);

  problem_.min_window_density = 0.1;
  EXPECT_FALSE(solve(problem_).solved);
}

TEST_P(UnattainableTileMinimumTest, OmitsTileMaximumExceededByExistingMetal)
{
  problem_.max_tile_density = 0.3;
  problem_.feature_areas = {4.0, 0.0, 0.0};
  problem_.max_fill_areas = {2.0, 10.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_LE(result.fill_areas[0], 2.0 + 1e-6);
  EXPECT_LE(result.fill_areas[1], 3.0 + 1e-6);
  EXPECT_LE(result.fill_areas[2], 3.0 + 1e-6);
  const auto min_var = solveMinVarLP(problem_);
  ASSERT_TRUE(min_var.solved);
  EXPECT_NEAR(min_var.fill_areas[0], 2.0, 1e-6);
}

TEST_P(UnattainableTileMinimumTest, OmitsWindowMaximumExceededByExistingMetal)
{
  problem_.max_window_density = 0.3;
  problem_.feature_areas = {7.0, 0.0, 0.0};
  problem_.max_fill_areas = {0.0, 10.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_GE(result.fill_areas[1], 2.0 - 1e-6);
  EXPECT_GE(result.fill_areas[2], 2.0 - 1e-6);
  EXPECT_LE(result.fill_areas[1] + result.fill_areas[2], 6.0 + 1e-6);
}

TEST_P(UnattainableTileMinimumTest, RetainsTileMaximumAtExistingLimit)
{
  problem_.max_tile_density = 0.3;
  problem_.feature_areas = {3.0, 0.0, 0.0};
  problem_.max_fill_areas = {7.0, 10.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-6);
}

TEST_P(UnattainableTileMinimumTest, RetainsWindowMaximumAtExistingLimit)
{
  problem_.min_tile_density = 0.0;
  problem_.feature_areas = {6.0, 6.0, 0.0};
  problem_.max_fill_areas = {4.0, 4.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-6);
  EXPECT_NEAR(result.fill_areas[1], 0.0, 1e-6);
}

INSTANTIATE_TEST_SUITE_P(AllPlanners,
                         UnattainableTileMinimumTest,
                         testing::Values(0, 1, 2));

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

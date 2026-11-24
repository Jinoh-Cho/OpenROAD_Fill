// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "FillCheck.h"
#include "FillCommon.h"
#include "LPFill.h"
#include "fin/Finale.h"
#include "gtest/gtest.h"

namespace fin {
namespace {
class FillLPTest
    : public testing::TestWithParam<std::tuple<std::string, std::string, int>>
{
 protected:
  LPFillOptions options() const
  {
    LPFillOptions result;
    result.rules_file = "unused.json";
    result.objective = std::get<0>(GetParam());
    result.solver = std::get<1>(GetParam());
    result.solver_threads = std::get<2>(GetParam());
    result.min_density = 0.3;
    result.max_density = 0.8;
    return result;
  }
};

TEST_P(FillLPTest, ObjectivesRespectExistingMetalAndWindowBounds)
{
  const FillLPProblem problem{
      {10, 10}, {0, 8}, {10, 2}, {{{}, {0}}, {{}, {1}}}};
  const auto config = options();
  const auto result = solveFillLP(problem, config);
  ASSERT_TRUE(result.solved) << result.status;
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(
      result.fill_areas[0], config.objective == "min_fill" ? 3.0 : 8.0, 1e-4);
  EXPECT_NEAR(result.fill_areas[1], 0.0, 1e-4);
  EXPECT_EQ(result.relaxed_windows, 0);
}

TEST_P(FillLPTest, InsufficientCapacityRequiresAllAvailableFill)
{
  const FillLPProblem problem{{10, 10}, {1, 0}, {1, 0}, {{{}, {0}}, {{}, {1}}}};
  const auto result = solveFillLP(problem, options());
  ASSERT_TRUE(result.solved) << result.status;
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(result.fill_areas[0], 1.0, 1e-4);
  EXPECT_NEAR(result.fill_areas[1], 0.0, 1e-4);
  EXPECT_EQ(result.relaxed_windows, 2);
}

TEST_P(FillLPTest, OverlappingWindowConflictsRemainInfeasible)
{
  const FillLPProblem problem{{100, 100, 100, 100, 100, 100},
                              {0, 0, 8, 0, 0, 0},
                              {1, 10, 10, 0, 0, 0},
                              {{{}, {0, 1, 3, 4}}, {{}, {1, 2, 4, 5}}}};
  auto config = options();
  config.min_density = 0.025;
  config.max_density = 0.03;
  const auto result = solveFillLP(problem, config);
  EXPECT_FALSE(result.solved);
  EXPECT_TRUE(result.fill_areas.empty());
}

TEST_P(FillLPTest, ExistingMetalAboveMaximumDoesNotRequireRemoval)
{
  const FillLPProblem problem{{10}, {9}, {1}, {{{}, {0}}}};
  const auto result = solveFillLP(problem, options());
  ASSERT_TRUE(result.solved) << result.status;
  EXPECT_GE(result.fill_areas[0], 0.0);
  EXPECT_LE(result.fill_areas[0], 1.0);
}

TEST_P(FillLPTest, UnitScalingPreservesTheDensitySolution)
{
  for (const double scale : {1e-6, 1.0, 15625000.0}) {
    const FillLPProblem problem{{10 * scale, 20 * scale},
                                {0, 16 * scale},
                                {10 * scale, 4 * scale},
                                {{{}, {0}}, {{}, {1}}}};
    const auto config = options();
    const auto result = solveFillLP(problem, config);
    ASSERT_TRUE(result.solved);
    EXPECT_NEAR(result.fill_areas[0] / scale,
                config.objective == "min_fill" ? 3.0 : 8.0,
                1e-4);
    EXPECT_NEAR(result.fill_areas[1] / scale, 0.0, 1e-4);
  }
}

TEST_P(FillLPTest, TileBoundsIncludeExistingMetal)
{
  for (const double scale : {1e-6, 1.0, 15625000.0}) {
    const FillLPProblem problem{{10 * scale, 10 * scale},
                                {0, 4 * scale},
                                {10 * scale, 6 * scale},
                                {{{}, {0, 1}}}};
    auto config = options();
    config.min_density = 0.0;
    config.max_density = 1.0;
    config.min_tile_density = 0.3;
    config.max_tile_density = 0.6;
    const auto result = solveFillLP(problem, config);
    ASSERT_TRUE(result.solved) << result.status;
    ASSERT_EQ(result.fill_areas.size(), 2);
    EXPECT_NEAR(result.fill_areas[0] / scale,
                config.objective == "min_fill" ? 3.0 : 6.0,
                1e-4);
    EXPECT_NEAR(result.fill_areas[1] / scale,
                config.objective == "min_fill" ? 0.0 : 2.0,
                1e-4);
    EXPECT_EQ(result.relaxed_tiles, 0);
  }
}

TEST_P(FillLPTest, TileMinimumRelaxesOnlyToCandidateCapacity)
{
  const FillLPProblem problem{{10, 10}, {0, 0}, {1, 10}, {{{}, {0, 1}}}};
  auto config = options();
  config.min_density = 0.0;
  config.max_density = 1.0;
  config.min_tile_density = 0.3;
  const auto result = solveFillLP(problem, config);
  ASSERT_TRUE(result.solved) << result.status;
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(result.fill_areas[0], 1.0, 1e-4);
  EXPECT_GE(result.fill_areas[1], 3.0 - 1e-4);
  EXPECT_EQ(result.relaxed_tiles, 1);
}

TEST_P(FillLPTest, TileAboveMaximumReceivesNoAdditionalFill)
{
  const FillLPProblem problem{{10, 10}, {9, 0}, {1, 10}, {{{}, {0, 1}}}};
  auto config = options();
  config.min_density = 0.0;
  config.max_density = 1.0;
  config.max_tile_density = 0.6;
  const auto result = solveFillLP(problem, config);
  ASSERT_TRUE(result.solved) << result.status;
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-4);
  EXPECT_LE(result.fill_areas[1], 6.0 + 1e-4);
}

TEST_P(FillLPTest, ConflictingWindowMinimumAndTileMaximumAreInfeasible)
{
  const FillLPProblem problem{{10, 10}, {0, 0}, {10, 10}, {{{}, {0, 1}}}};
  auto config = options();
  config.min_density = 0.8;
  config.max_density = 1.0;
  config.max_tile_density = 0.5;
  const auto result = solveFillLP(problem, config);
  EXPECT_FALSE(result.solved);
  EXPECT_TRUE(result.fill_areas.empty());
}

INSTANTIATE_TEST_SUITE_P(BothSolversAndObjectives,
                         FillLPTest,
                         testing::Combine(testing::Values("min_fill",
                                                          "min_var"),
                                          testing::Values("glop", "pdlp"),
                                          testing::Values(1, 4)));

TEST(FillLPOptionsTest, RejectsInvalidTileBounds)
{
  LPFillOptions options;
  options.rules_file = "unused.json";
  for (const double bad : {-1.0,
                           1.1,
                           std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    auto invalid = options;
    invalid.min_tile_density = bad;
    EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
    invalid = options;
    invalid.max_tile_density = bad;
    EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  }
  options.min_tile_density = 0.7;
  options.max_tile_density = 0.5;
  EXPECT_THROW(validateLPFillOptions(options), std::invalid_argument);
}

TEST(FillLPOptionsTest, RejectsInvalidOptions)
{
  LPFillOptions options;
  options.rules_file = "unused.json";
  EXPECT_NO_THROW(validateLPFillOptions(options));
  for (const double bad : {0.0,
                           -1.0,
                           std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    auto invalid = options;
    invalid.window_width = bad;
    EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
    invalid = options;
    invalid.step_y = bad;
    EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  }
  for (const std::string bad : {"", "lip1", "MIN_FILL"}) {
    auto invalid = options;
    invalid.objective = bad;
    EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  }
  auto invalid = options;
  invalid.solver = "unknown";
  EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  invalid = options;
  invalid.num_threads = 0;
  EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  invalid = options;
  invalid.solver_threads = -1;
  EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  invalid = options;
  invalid.min_density = 0.9;
  EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
  invalid = options;
  invalid.max_density = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(validateLPFillOptions(invalid), std::invalid_argument);
}

TEST(FillLPInputTest, RejectsInvalidTileIndicesAndAreas)
{
  LPFillOptions options;
  options.rules_file = "unused.json";
  FillLPProblem problem{{10}, {0}, {5}, {{{}, {0, 0}}}};
  EXPECT_THROW(solveFillLP(problem, options), std::invalid_argument);
  problem.windows[0].tiles = {1};
  EXPECT_THROW(solveFillLP(problem, options), std::invalid_argument);
  problem.windows[0].tiles = {0};
  problem.capacities[0] = -1;
  EXPECT_THROW(solveFillLP(problem, options), std::invalid_argument);
}

TEST(FillGridTest, NonSquareWindowsAndNonDivisibleStepsPartitionExactly)
{
  const FillGrid grid(
      odb::Rect(-5, -7, 32, 24), odb::Point(20, 13), odb::Point(7, 8));
  ASSERT_EQ(grid.windows().size(), 9);
  for (const auto& window : grid.windows()) {
    double area = 0.0;
    for (const size_t tile : window.tiles) {
      const auto& rect = grid.tiles()[tile];
      EXPECT_TRUE(window.bounds.contains(rect));
      area += rect.area();
    }
    EXPECT_DOUBLE_EQ(area, 260.0);
  }
  const auto areas = grid.metalAreas({Rectangle(-100, -100, 100, 100)}, 4);
  for (const double density : grid.windowDensities(areas)) {
    EXPECT_DOUBLE_EQ(density, 1.0);
  }
}

TEST(FillGridTest, PartitionedUnionMatchesFullGeometricUnion)
{
  const FillGrid grid(
      odb::Rect(0, 0, 30, 25), odb::Point(17, 11), odb::Point(8, 6));
  const std::vector<Rectangle> source{Rectangle(-3, 4, 20, 18),
                                      Rectangle(8, -2, 22, 23),
                                      Rectangle(25, 20, 40, 40)};
  const auto serial = grid.metalAreas(source, 1);
  const auto parallel = grid.metalAreas(source, 4);
  EXPECT_EQ(serial, parallel);
  Polygon90Set metal;
  for (const auto& rect : source) {
    metal.insert(rect);
  }
  const auto densities = grid.windowDensities(serial);
  for (size_t i = 0; i < densities.size(); ++i) {
    const auto& rect = grid.windows()[i].bounds;
    Polygon90Set window;
    window.insert(
        Rectangle(rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax()));
    Polygon90Set clipped = metal & window;
    EXPECT_NEAR(
        densities[i],
        boost::polygon::area(clipped) / static_cast<double>(rect.area()),
        1e-12);
  }
}

TEST(FillGridTest, RejectsInvalidGeometry)
{
  EXPECT_THROW(
      FillGrid(odb::Rect(0, 0, 10, 10), odb::Point(20, 5), odb::Point(1, 1)),
      std::invalid_argument);
  EXPECT_THROW(
      FillGrid(odb::Rect(0, 0, 10, 10), odb::Point(5, 5), odb::Point(0, 1)),
      std::invalid_argument);
}

TEST(FillSelectionTest, RespectsAreaAndRetainsCandidateOrder)
{
  const std::vector<Rectangle> candidates{Rectangle(0, 0, 2, 2),
                                          Rectangle(3, 0, 5, 2),
                                          Rectangle(6, 0, 8, 2),
                                          Rectangle(9, 0, 11, 2)};
  const auto selected = selectFillCandidates(candidates, 13.0);
  ASSERT_EQ(selected.size(), 3);
  EXPECT_EQ(
      selected,
      (std::vector<Rectangle>(candidates.begin(), candidates.begin() + 3)));
  EXPECT_TRUE(selectFillCandidates(candidates, 0.0).empty());
}

TEST(FillSelectionTest, SkipsShapesThatExceedRemainingBudget)
{
  const std::vector<Rectangle> candidates{
      Rectangle(0, 0, 3, 2), Rectangle(4, 0, 5, 1), Rectangle(6, 0, 8, 2)};
  const auto selected = selectFillCandidates(candidates, 5.0);
  EXPECT_EQ(selected, (std::vector<Rectangle>{candidates[1], candidates[2]}));
  for (const double bad : {-1.0,
                           std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(selectFillCandidates(candidates, bad), std::invalid_argument);
  }
}

TEST(FillPatternTest, AlternatesRowsByTheSpecifiedPhysicalXOffset)
{
  FillShapesConfig config;
  config.stagger = true;
  config.stagger_offset = odb::Point(6, 0);
  const auto pattern
      = makeFillPattern(Rectangle(0, 0, 48, 48), 10, 10, 2, 2, config);
  std::vector<Rectangle> actual;
  pattern.get_rectangles(actual);
  ASSERT_EQ(actual.size(), 14);
  for (const auto& rect : actual) {
    EXPECT_EQ(yl(rect) % 12, 0);
    EXPECT_EQ(xl(rect) % 12, (yl(rect) / 12) % 2 == 0 ? 0 : 6);
  }
  EXPECT_NE(std::find(actual.begin(), actual.end(), Rectangle(6, 12, 16, 22)),
            actual.end());
  const auto negative
      = makeFillPattern(Rectangle(-24, -24, 24, 24), 10, 10, 2, 2, config);
  actual.clear();
  negative.get_rectangles(actual);
  EXPECT_NE(std::find(actual.begin(), actual.end(), Rectangle(-6, -12, 4, -2)),
            actual.end());
}

TEST(FillPatternTest, AlternatesColumnsByTheSpecifiedPhysicalYOffset)
{
  FillShapesConfig config;
  config.stagger = true;
  config.stagger_offset = odb::Point(0, 6);
  const auto pattern
      = makeFillPattern(Rectangle(0, 0, 48, 48), 10, 10, 2, 2, config);
  std::vector<Rectangle> actual;
  pattern.get_rectangles(actual);
  ASSERT_EQ(actual.size(), 14);
  for (const auto& rect : actual) {
    EXPECT_EQ(xl(rect) % 12, 0);
    EXPECT_EQ(yl(rect) % 12, (xl(rect) / 12) % 2 == 0 ? 0 : 6);
  }
}

TEST(FillPatternTest, RejectsSimultaneousXAndYOffsets)
{
  FillShapesConfig config;
  config.stagger = true;
  config.stagger_offset = odb::Point(6, 6);
  EXPECT_THROW(
      makeFillPattern(Rectangle(-48, -48, 48, 48), 10, 10, 2, 2, config),
      std::invalid_argument);
}

TEST(FillPatternTest, SequentialSelectionRetainsStaggerCandidateOrder)
{
  FillShapesConfig config;
  config.stagger = true;
  config.stagger_offset = odb::Point(6, 0);
  const auto pattern
      = makeFillPattern(Rectangle(0, 0, 66, 58), 10, 10, 2, 2, config);
  std::vector<Rectangle> sites;
  pattern.get_rectangles(sites);
  ASSERT_EQ(sites.size(), 25);
  const auto selected = selectFillCandidates(sites, 100);
  ASSERT_EQ(selected.size(), 1);
  EXPECT_EQ(selected[0], sites.front());
}

TEST(FillThreadTest, PropagatesWorkerExceptions)
{
  EXPECT_THROW(
      forEachFillTile(4, 2, [](size_t) { throw std::runtime_error("test"); }),
      std::runtime_error);
}
}  // namespace
}  // namespace fin

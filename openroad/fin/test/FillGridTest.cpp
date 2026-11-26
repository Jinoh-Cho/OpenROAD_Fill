// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"
#include "openroad/fin/src/FillCheck.h"

namespace fin {
namespace {

TEST(FillGridTest, ResolutionIsPreservedWhenWindowIsCloseToCoreSize)
{
  for (const int core_size : {100, 150, 200}) {
    const FillGrid grid(odb::Rect(0, 0, core_size, core_size),
                        odb::Point(100, 100),
                        odb::Point(25, 25));
    const size_t tiles_per_side = core_size / 25;
    const size_t windows_per_side = (core_size - 100) / 25 + 1;
    EXPECT_EQ(grid.tiles().size(), tiles_per_side * tiles_per_side);
    EXPECT_EQ(grid.windows().size(), windows_per_side * windows_per_side);
    for (const auto& window : grid.windows()) {
      ASSERT_EQ(window.tiles.size(), 16);
      int64_t area = 0;
      for (const size_t index : window.tiles) {
        const auto& tile = grid.tiles()[index];
        EXPECT_EQ(tile.dx(), 25);
        EXPECT_EQ(tile.dy(), 25);
        EXPECT_TRUE(window.bounds.contains(tile));
        area += tile.area();
      }
      EXPECT_EQ(area, window.bounds.area());
    }
    const std::vector<double> metal_areas(grid.tiles().size(), 312.5);
    for (const double density : grid.windowDensities(metal_areas)) {
      EXPECT_DOUBLE_EQ(density, 0.5);
    }
  }
}

TEST(FillGridTest, OneAxisCanHaveOnlyOneWindow)
{
  const FillGrid grid(
      odb::Rect(0, 0, 100, 200), odb::Point(100, 100), odb::Point(25, 25));
  EXPECT_EQ(grid.tiles().size(), 32);
  EXPECT_EQ(grid.windows().size(), 5);
  for (const auto& window : grid.windows()) {
    EXPECT_EQ(window.tiles.size(), 16);
  }
}

TEST(FillGridTest, NegativeOriginAndClippedBoundaryPreserveWindowTiles)
{
  const FillGrid grid(
      odb::Rect(-13, -7, 98, 104), odb::Point(100, 100), odb::Point(25, 25));
  ASSERT_EQ(grid.windows().size(), 1);
  EXPECT_EQ(grid.windows().front().tiles.size(), 16);
  ASSERT_EQ(grid.tiles().size(), 25);
  EXPECT_EQ(grid.tiles().back().dx(), 11);
  EXPECT_EQ(grid.tiles().back().dy(), 11);
  for (const size_t index : grid.windows().front().tiles) {
    EXPECT_EQ(grid.tiles()[index].area(), 625);
  }
}

TEST(FillGridTest, InvalidGridDimensionsAreRejected)
{
  const odb::Rect core(0, 0, 100, 100);
  const odb::Point window(100, 100);
  EXPECT_THROW(FillGrid(core, window, odb::Point(0, 25)),
               std::invalid_argument);
  EXPECT_THROW(FillGrid(core, window, odb::Point(30, 25)),
               std::invalid_argument);
  EXPECT_THROW(FillGrid(core, odb::Point(125, 100), odb::Point(25, 25)),
               std::invalid_argument);
}

}
}

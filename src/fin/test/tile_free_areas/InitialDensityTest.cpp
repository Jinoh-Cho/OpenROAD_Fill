// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "FillGeometry.h"
#include "LPFillUtil.h"
#include "TileGrid.h"
#include "gtest/gtest.h"
namespace fin {
namespace {
void expectSameDensity(const TileGrid& old, const TileGrid& tiled)
{
  EXPECT_EQ(old.metalAreas(), tiled.metalAreas());
  EXPECT_EQ(old.metalDensities(), tiled.metalDensities());
  EXPECT_DOUBLE_EQ(old.totalMetalArea(), tiled.totalMetalArea());
  ASSERT_EQ(old.windows().size(), tiled.windows().size());
  for (size_t i = 0; i < old.windows().size(); ++i) {
    EXPECT_DOUBLE_EQ(old.windows()[i].metal_area, tiled.windows()[i].metal_area)
        << i;
    EXPECT_DOUBLE_EQ(old.windows()[i].density, tiled.windows()[i].density) << i;
  }
}
TEST(InitialDensityTest, ExactAreasAndWindowsForClippedOverlappingGeometry)
{
  for (const auto origin : {odb::Point(0, 0), odb::Point(3, 5)}) {
    const TileGridConfig config{odb::Rect(-13, -7, 97, 85), origin, 40, 4};
    for (int scenario = 0; scenario < 4; ++scenario) {
      std::vector<Rectangle> rectangles;
      if (scenario == 1) {
        rectangles.emplace_back(-100, -100, 200, 200);
      }
      if (scenario >= 2) {
        rectangles = {{-20, -30, -11, 100},
                      {-5, -5, 23, 7},
                      {-5, -5, 23, 7},
                      {20, 10, 45, 15},
                      {20, 25, 45, 30},
                      {20, 10, 25, 30},
                      {40, 10, 45, 30},
                      {97, 0, 110, 100},
                      {0, 85, 97, 100}};
      }
      if (scenario == 3) {
        std::mt19937 rng(42);
        for (int i = 0; i < 200; ++i) {
          const int x = int(rng() % 160) - 30, y = int(rng() % 160) - 30;
          const int w = 1 + rng() % 20, h = 1 + rng() % 20;
          rectangles.emplace_back(x, y, x + w, y + h);
        }
      }
      Polygon90Set metal;
      metal.insert(rectangles.begin(), rectangles.end());
      TileGrid legacy(config);
      legacy.calculateMetalDensities(metal);
      for (int threads : {1, 16, 32}) {
        TileGrid tiled(config);
        std::map<std::string, double> phases;
        const int actual = tiled.calculateMetalDensitiesFromRectangles(
            rectangles, threads, &phases);
        expectSameDensity(legacy, tiled);
        EXPECT_GE(actual, 1);
        EXPECT_LE(actual, threads);
        EXPECT_DOUBLE_EQ(phases.at("post_fill_union_clip_seconds"), 0.0);
        for (const auto& [name, elapsed] : phases) {
          EXPECT_GE(elapsed, 0.0) << name;
        }
      }
    }
  }
  TileGrid empty({odb::Rect(0, 0, 0, 0), odb::Point(0, 0), 40, 4});
  EXPECT_EQ(empty.calculateMetalDensitiesFromRectangles({}, 16), 1);
  EXPECT_TRUE(empty.metalAreas().empty());
  EXPECT_THROW(empty.calculateMetalDensitiesFromRectangles({}, 0),
               std::invalid_argument);
}
TEST(InitialDensityTest, BenchmarkExistingSmallOdb)
{
  const char* path = std::getenv("FIN_METAL_TEST_ODB");
  if (!path) {
    GTEST_SKIP() << "Set FIN_METAL_TEST_ODB.";
  }
  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)> db(
      odb::dbDatabase::create(), &odb::dbDatabase::destroy);
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input.good());
  db->read(input);
  auto* block = db->getChip()->getBlock();
  auto* layer = block->getTech()->findLayer("M2");
  ASSERT_NE(layer, nullptr);
  std::vector<Rectangle> originals;
  const auto metal = orNonFills(block,
                                layer,
                                nullptr,
                                16,
                                nullptr,
                                nullptr,
                                0,
                                nullptr,
                                nullptr,
                                &originals);
  const int dbu = block->getDbUnitsPerMicron();
  const TileGridConfig config{
      block->getDieArea(), odb::Point(0, 0), 50 * dbu, 4};
  using Clock = std::chrono::steady_clock;
  for (int threads : {1, 16, 32}) {
    for (int repeat = 0; repeat < 3; ++repeat) {
      TileGrid legacy(config), tiled(config);
      double legacy_seconds = 0, tiled_seconds = 0;
      int actual = 0;
      std::map<std::string, double> phases;
      auto old_run = [&] {
        const Polygon90Set raw = metal;
        const auto start = Clock::now();
        legacy.calculateMetalDensities(raw);
        legacy_seconds
            = std::chrono::duration<double>(Clock::now() - start).count();
      };
      auto new_run = [&] {
        const auto start = Clock::now();
        actual = tiled.calculateMetalDensitiesFromRectangles(
            originals, threads, &phases);
        tiled_seconds
            = std::chrono::duration<double>(Clock::now() - start).count();
      };
      if (repeat % 2) {
        new_run();
        old_run();
      } else {
        old_run();
        new_run();
      }
      expectSameDensity(legacy, tiled);
      std::cout << "INITIAL_DENSITY_BENCH: threads=" << threads
                << " actual=" << actual << " repeat=" << repeat
                << " rectangles=" << originals.size()
                << " tiles=" << tiled.tiles().size()
                << " legacy_seconds=" << legacy_seconds
                << " tiled_seconds=" << tiled_seconds << std::endl;
      for (const auto& [name, value] : phases) {
        std::cout << "TILED " << name << "=" << value << std::endl;
      }
    }
  }
  // Check the full collected input also retains the established candidate list.
  const int spacing = int(.018 * dbu);
  const FillShapesConfig rules{{{int(.16 * dbu), int(.16 * dbu)},
                                {int(.08 * dbu), int(.08 * dbu)},
                                {int(.04 * dbu), int(.04 * dbu)}},
                               spacing,
                               spacing,
                               0};
  std::vector<Rectangle> expanded;
  for (const auto& r : originals) {
    expanded.emplace_back(
        xl(r) - spacing, yl(r) - spacing, xh(r) + spacing, yh(r) + spacing);
  }
  const Polygon90Set blocked = metal + spacing;
  const TileGrid grid(config);
  auto old_areas
      = lpfill::makeLayerFillAreas(grid, blocked, layer, rules, nullptr, 16);
  auto new_areas = lpfill::makeLayerFillAreasFromRectangles(
      grid, expanded, layer, rules, nullptr, 16);
  EXPECT_EQ(lpfill::makeLayerFillCandidates(
                old_areas.tile_areas, layer, rules, nullptr, 16)
                .tiles,
            lpfill::makeLayerFillCandidates(
                new_areas.tile_areas, layer, rules, nullptr, 16)
                .tiles);
}
}  // namespace
}  // namespace fin

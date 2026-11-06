// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

#include "FillGeometry.h"
#include "LPFillUtil.h"
#include "TileGrid.h"
#include "gtest/gtest.h"
namespace fin {
namespace {
void expectEquivalent(const lpfill::LayerFillAreas& global,
                      const lpfill::LayerFillAreas& tiled)
{
  ASSERT_EQ(global.tile_areas.size(), tiled.tile_areas.size());
  EXPECT_EQ(global.bloated_non_fill_areas, tiled.bloated_non_fill_areas);
  for (size_t i = 0; i < global.tile_areas.size(); ++i) {
    Polygon90Set missing = global.tile_areas[i] - tiled.tile_areas[i];
    Polygon90Set extra = tiled.tile_areas[i] - global.tile_areas[i];
    EXPECT_EQ(boost::polygon::area(missing), 0) << "tile=" << i;
    EXPECT_EQ(boost::polygon::area(extra), 0) << "tile=" << i;
  }
}
TEST(TileFreeAreasTest, GeometryAndCandidatesMatchAtClippedAndCrossingTiles)
{
  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)> db(
      odb::dbDatabase::create(), &odb::dbDatabase::destroy);
  auto* tech = odb::dbTech::create(db.get(), "tech");
  auto* layer
      = odb::dbTechLayer::create(tech, "M2", odb::dbTechLayerType::ROUTING);
  const TileGrid grid({odb::Rect(-13, -7, 97, 85), odb::Point(0, 0), 40, 4});
  const FillShapesConfig config{{{4, 2}, {2, 2}}, 1, 2, 3};
  for (auto direction :
       {odb::dbTechLayerDir::HORIZONTAL, odb::dbTechLayerDir::VERTICAL}) {
    layer->setDirection(direction);
    for (int scenario = 0; scenario < 4; ++scenario) {
      std::vector<Rectangle> rectangles;
      if (scenario == 1) {
        rectangles.emplace_back(-100, -100, 200, 200);
      }
      if (scenario >= 2) {
        rectangles = {{-17, -30, -11, 100},
                      {-5, -5, 23, 7},
                      {10, -10, 15, 35},
                      {20, 10, 45, 15},
                      {20, 25, 45, 30},
                      {20, 10, 25, 30},
                      {40, 10, 45, 30},
                      {97, 0, 110, 100},
                      {0, 85, 97, 100}};
      }
      if (scenario == 3) {
        std::mt19937 random(42);
        for (int i = 0; i < 100; ++i) {
          const int x = static_cast<int>(random() % 160) - 30;
          const int y = static_cast<int>(random() % 160) - 30;
          const int width = 1 + random() % 12, height = 1 + random() % 12;
          rectangles.emplace_back(x, y, x + width, y + height);
        }
      }
      Polygon90Set obstacles;
      for (const auto& rectangle : rectangles) {
        obstacles.insert(rectangle);
      }
      auto global = lpfill::makeLayerFillAreas(
          grid, obstacles, layer, config, nullptr, 16);
      const auto expected = lpfill::makeLayerFillCandidates(
          global.tile_areas, layer, config, nullptr, 16);
      for (int threads : {1, 16}) {
        const auto tiled = lpfill::makeLayerFillAreasFromRectangles(
            grid, rectangles, layer, config, nullptr, threads);
        expectEquivalent(global, tiled);
        EXPECT_EQ(expected.tiles,
                  lpfill::makeLayerFillCandidates(
                      tiled.tile_areas, layer, config, nullptr, threads)
                      .tiles);
      }
    }
  }
}
TEST(TileFreeAreasTest, BenchmarkExistingSmallOdb)
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
  const int dbu = block->getDbUnitsPerMicron();
  const int spacing = static_cast<int>(.018 * dbu);
  const FillShapesConfig config{{{int(.16 * dbu), int(.16 * dbu)},
                                 {int(.08 * dbu), int(.08 * dbu)},
                                 {int(.04 * dbu), int(.04 * dbu)}},
                                spacing,
                                spacing,
                                0};
  const TileGrid grid({block->getDieArea(), odb::Point(0, 0), 50 * dbu, 4});
  Polygon90Set expanded;
  std::vector<Rectangle> rectangles;
  bool fused = false;
  orNonFills(block,
             layer,
             nullptr,
             16,
             nullptr,
             &expanded,
             spacing,
             &fused,
             &rectangles);
  using Clock = std::chrono::steady_clock;
  for (int threads : {1, 16}) {
    for (int repeat = 0; repeat < 3; ++repeat) {
      lpfill::LayerFillAreas global, tiled;
      double global_seconds = 0, tiled_seconds = 0;
      auto run_global = [&] {
        // Fresh raw edges for each trial; exclude the benchmark's reference
        // copy.
        const Polygon90Set raw = expanded;
        const auto start = Clock::now();
        global = lpfill::makeLayerFillAreas(
            grid, raw, layer, config, nullptr, threads);
        global_seconds
            = std::chrono::duration<double>(Clock::now() - start).count();
      };
      auto run_tiled = [&] {
        const auto start = Clock::now();
        tiled = lpfill::makeLayerFillAreasFromRectangles(
            grid, rectangles, layer, config, nullptr, threads);
        tiled_seconds
            = std::chrono::duration<double>(Clock::now() - start).count();
      };
      if (repeat % 2) {
        run_tiled();
        run_global();
      } else {
        run_global();
        run_tiled();
      }
      std::cout << "TILE_FREE_BENCH: threads=" << threads
                << " repeat=" << repeat << " rectangles=" << rectangles.size()
                << " tiles=" << grid.tiles().size()
                << " global_seconds=" << global_seconds
                << " tiled_seconds=" << tiled_seconds << " fused=" << fused
                << std::endl;
      for (const auto& [name, value] : global.phase_seconds) {
        std::cout << "GLOBAL " << name << "=" << value << std::endl;
      }
      for (const auto& [name, value] : tiled.phase_seconds) {
        std::cout << "TILED " << name << "=" << value << std::endl;
      }
      expectEquivalent(global, tiled);
      if (repeat == 0) {
        auto a = lpfill::makeLayerFillCandidates(
            global.tile_areas, layer, config, nullptr, threads);
        auto b = lpfill::makeLayerFillCandidates(
            tiled.tile_areas, layer, config, nullptr, threads);
        EXPECT_EQ(a.tiles, b.tiles);
      }
    }
  }
}
}  // namespace
}  // namespace fin

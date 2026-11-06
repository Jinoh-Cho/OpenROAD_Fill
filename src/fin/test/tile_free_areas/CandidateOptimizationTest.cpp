// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <omp.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <vector>

#include "CandidateReference.h"
#include "FillGeometry.h"
#include "LPFillUtil.h"
#include "TileGrid.h"
#include "gtest/gtest.h"
namespace fin {
namespace {
TEST(CandidateFinalSizeTest, PreservesCoordinatesAndOrderForEverySizeSequence)
{
  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)> db(
      odb::dbDatabase::create(), &odb::dbDatabase::destroy);
  auto* tech = odb::dbTech::create(db.get(), "tech");
  auto* layer
      = odb::dbTechLayer::create(tech, "M2", odb::dbTechLayerType::ROUTING);
  const std::vector<std::vector<std::pair<int, int>>> sequences
      = {{},
         {{4, 2}},
         {{8, 4}, {4, 2}, {2, 2}},
         {{2, 2}, {4, 2}, {8, 4}},
         {{4, 2}, {4, 2}},
         {{7, 3}, {3, 3}},
         {{4, 2}, {100, 100}}};
  for (auto direction :
       {odb::dbTechLayerDir::HORIZONTAL, odb::dbTechLayerDir::VERTICAL}) {
    layer->setDirection(direction);
    for (int scenario = 0; scenario < 4; ++scenario) {
      Polygon90Set area;
      area.insert(Rectangle(-40, -30, 80, 70));
      if (scenario >= 1) {
        area -= Rectangle(-10, -10, 30, 30);
      }
      if (scenario >= 2) {
        area.insert(Rectangle(100, -30, 130, 20));
      }
      if (scenario == 3) {
        std::mt19937 rng(123);
        for (int i = 0; i < 30; ++i) {
          const int x = int(rng() % 100) - 30, y = int(rng() % 80) - 20;
          area -= Rectangle(x, y, x + 2 + rng() % 12, y + 2 + rng() % 12);
        }
      }
      std::vector<Polygon90> polygons;
      area.get(polygons);
      for (const auto& sizes : sequences) {
        const FillShapesConfig config{sizes, 1, 2, 3};
        for (const auto& polygon : polygons) {
          const auto expected
              = lpfill::test_reference::makeFillCandidatesBefore(
                  polygon, layer, config, nullptr);
          const auto actual
              = lpfill::makeFillCandidates(polygon, layer, config, nullptr);
          ASSERT_TRUE(expected == actual)
              << "scenario=" << scenario << " sizes=" << sizes.size();
        }
      }
    }
  }
}
struct Generated
{
  std::vector<std::vector<Rectangle>> tiles;
  int threads = 1;
};
Generated generate(const std::vector<Polygon90Set>& areas,
                   odb::dbTechLayer* layer,
                   const FillShapesConfig& config,
                   int threads,
                   bool reference)
{
  Generated result;
  result.tiles.resize(areas.size());
  std::exception_ptr error;
  std::mutex mutex;
  std::atomic<bool> failed{false};
#pragma omp parallel num_threads(threads)
  {
#pragma omp single
    result.threads = omp_get_num_threads();
#pragma omp for schedule(dynamic, 16)
    for (size_t i = 0; i < areas.size(); ++i) {
      if (failed.load(std::memory_order_relaxed)) {
        continue;
      }
      try {
        result.tiles[i]
            = reference ? lpfill::test_reference::makeTileFillCandidatesBefore(
                  areas[i], layer, config, nullptr)
                        : lpfill::makeTileFillCandidates(
                            areas[i], layer, config, nullptr);
      } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!error) {
          error = std::current_exception();
        }
        failed.store(true, std::memory_order_relaxed);
      }
    }
  }
  if (error) {
    std::rethrow_exception(error);
  }
  return result;
}
TEST(CandidateFinalSizeTest, BenchmarkExistingSmallOdb)
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
  const int dbu = block->getDbUnitsPerMicron(), spacing = int(.018 * dbu);
  const FillShapesConfig config{{{int(.16 * dbu), int(.16 * dbu)},
                                 {int(.08 * dbu), int(.08 * dbu)},
                                 {int(.04 * dbu), int(.04 * dbu)}},
                                spacing,
                                spacing,
                                0};
  const TileGrid grid({block->getDieArea(), odb::Point(0, 0), 50 * dbu, 4});
  std::vector<Rectangle> rectangles;
  orNonFills(block,
             layer,
             nullptr,
             16,
             nullptr,
             nullptr,
             spacing,
             nullptr,
             &rectangles);
  auto prepared = lpfill::makeLayerFillAreasFromRectangles(
      grid, rectangles, layer, config, nullptr, 16);
  using Clock = std::chrono::steady_clock;
  std::vector<int> thread_counts{1, 16, 32};
  if (const char* counts = std::getenv("FIN_CANDIDATE_BENCH_THREADS")) {
    thread_counts.clear();
    std::istringstream stream(counts);
    int threads;
    while (stream >> threads) {
      ASSERT_GT(threads, 0);
      thread_counts.push_back(threads);
    }
    ASSERT_TRUE(stream.eof());
    ASSERT_FALSE(thread_counts.empty());
  }
  for (int threads : thread_counts) {
    for (int repeat = 0; repeat < 3; ++repeat) {
      Generated before, after;
      double old_seconds = 0, new_seconds = 0;
      auto old_run = [&] {
        const auto input_areas = prepared.tile_areas;
        const auto start = Clock::now();
        before = generate(input_areas, layer, config, threads, true);
        old_seconds
            = std::chrono::duration<double>(Clock::now() - start).count();
      };
      auto new_run = [&] {
        const auto input_areas = prepared.tile_areas;
        const auto start = Clock::now();
        after = generate(input_areas, layer, config, threads, false);
        new_seconds
            = std::chrono::duration<double>(Clock::now() - start).count();
      };
      if (repeat % 2) {
        new_run();
        old_run();
      } else {
        old_run();
        new_run();
      }
      ASSERT_EQ(before.tiles.size(), after.tiles.size());
      size_t count = 0;
      for (size_t i = 0; i < before.tiles.size(); ++i) {
        ASSERT_TRUE(before.tiles[i] == after.tiles[i]) << "tile=" << i;
        count += after.tiles[i].size();
      }
      std::cout << "CANDIDATE_FINAL_SIZE_BENCH: threads=" << threads
                << " actual=" << after.threads << " repeat=" << repeat
                << " tiles=" << after.tiles.size() << " candidates=" << count
                << " old_seconds=" << old_seconds
                << " new_seconds=" << new_seconds << std::endl;
    }
  }
}
}  // namespace
}  // namespace fin

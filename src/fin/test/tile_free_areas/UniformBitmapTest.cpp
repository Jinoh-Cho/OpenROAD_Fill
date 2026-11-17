// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <omp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "CandidateSelection.h"
#include "UniformBitmapReference.h"
#include "UniformSelectionReference.h"
#include "gtest/gtest.h"

namespace fin::lpfill {
namespace {
void expectReference(const std::vector<Rectangle>& candidates, double target)
{
  std::vector<Rectangle> reference, bitmap, hash;
  const double expected = selection_reference::selectUniformCandidates(
      candidates, target, 2, 2, reference);
  const double actual
      = selectUniformCandidates(candidates, target, 2, 2, bitmap);
  const double fallback = selectUniformCandidates(
      candidates, target, 2, 2, hash, UniformBackend::Hash);
  EXPECT_EQ(expected, actual);
  EXPECT_EQ(expected, fallback);
  EXPECT_TRUE(reference == bitmap);
  EXPECT_TRUE(reference == hash);
  for (int bits = 0; bits < 8; ++bits) {
    std::vector<Rectangle> optimized;
    const UniformOptimizations options{
        bool(bits & 1), bool(bits & 2), bool(bits & 4)};
    const double area = selectUniformCandidates(candidates,
                                                target,
                                                2,
                                                2,
                                                optimized,
                                                UniformBackend::Auto,
                                                nullptr,
                                                options);
    EXPECT_EQ(expected, area) << "optimizations=" << bits;
    EXPECT_TRUE(reference == optimized) << "optimizations=" << bits;
  }
}

TEST(UniformBitmapTest, MatchesFrozenHashForHolesSizesOffsetsAndInputOrders)
{
  std::mt19937 rng(12345);
  for (int scenario = 0; scenario < 24; ++scenario) {
    std::vector<Rectangle> candidates;
    for (int size : {2, 3, 5}) {
      for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 19; ++x) {
          if (scenario % 3 == 0 && rng() % 4 == 0) {
            continue;
          }
          const int offset = scenario % 2 ? -100003 : 1000000000;
          const int left = offset + x * (size + 2);
          const int bottom = offset + y * (size + 2);
          candidates.emplace_back(left, bottom, left + size, bottom + size);
        }
      }
    }
    std::shuffle(candidates.begin(), candidates.end(), rng);
    for (double target : {0.0, .0005, .002, 4.0, 31.25, 157.0, 1e9}) {
      expectReference(candidates, target);
    }
  }
}

TEST(UniformBitmapTest, ChoosesDenseBitmapAndSparseFallback)
{
  std::vector<Rectangle> dense;
  for (int y = 0; y < 11; ++y) {
    for (int x = 0; x < 13; ++x) {
      dense.emplace_back(x * 4, y * 4, x * 4 + 2, y * 4 + 2);
    }
  }
  UniformSelectionStats stats;
  std::vector<Rectangle> selected;
  selectUniformCandidates(
      dense, 50, 2, 2, selected, UniformBackend::Auto, &stats);
  EXPECT_EQ(stats.bitmap_groups, 1);
  EXPECT_EQ(stats.hash_groups, 0);
  const std::vector<Rectangle> sparse{{-1000000000, 0, -999999998, 2},
                                      {1000000000, 0, 1000000002, 2}};
  selected.clear();
  selectUniformCandidates(sparse,
                          8,
                          2,
                          2,
                          selected,
                          UniformBackend::Auto,
                          &stats,
                          {false, true, true});
  EXPECT_EQ(stats.hash_groups, 1);
  expectReference(sparse, 8);
  selected.clear();
  selectUniformCandidates(
      sparse, 8, 2, 2, selected, UniformBackend::Auto, &stats);
  EXPECT_EQ(stats.small_groups, 1);
  for (int axis = 0; axis < 2; ++axis) {
    std::vector<Rectangle> thin;
    for (int i = 0; i < 20; ++i) {
      thin.emplace_back(axis ? i * 4 : 0,
                        axis ? 0 : i * 4,
                        (axis ? i * 4 : 0) + 2,
                        (axis ? 0 : i * 4) + 2);
    }
    expectReference(thin, 39);
  }
}

struct Trial
{
  std::vector<std::vector<Rectangle>> selected;
  std::vector<double> areas;
  std::vector<UniformSelectionStats> stats;
  int actual_threads = 1;
  double seconds = 0;
};

Trial runSelection(const std::vector<std::vector<Rectangle>>& candidates,
                   const std::vector<double>& targets,
                   int threads,
                   int mode)
{
  Trial trial;
  trial.selected.resize(candidates.size());
  trial.areas.resize(candidates.size());
  trial.stats.resize(candidates.size());
  std::exception_ptr error;
  std::mutex mutex;
  const auto start = std::chrono::steady_clock::now();
#pragma omp parallel num_threads(threads)
  {
#pragma omp single
    trial.actual_threads = omp_get_num_threads();
#pragma omp for schedule(dynamic, 16)
    for (size_t i = 0; i < candidates.size(); ++i) {
      try {
        if (mode == 0) {
          trial.areas[i] = selectTileCandidates(candidates[i],
                                                targets[i],
                                                SelectionPolicy::Greedy,
                                                18,
                                                18,
                                                trial.selected[i]);
          continue;
        }
        if (mode == 1) {
          trial.areas[i] = selection_reference::selectUniformCandidates(
              candidates[i], targets[i], 18, 18, trial.selected[i]);
        } else if (mode == 2) {
          trial.areas[i] = bitmap_reference::selectUniformCandidates(
              candidates[i], targets[i], 18, 18, trial.selected[i]);
        } else {
          const UniformOptimizations options{mode == 3 || mode == 6,
                                             mode == 4 || mode == 6,
                                             mode == 5 || mode == 6};
          trial.areas[i] = selectUniformCandidates(candidates[i],
                                                   targets[i],
                                                   18,
                                                   18,
                                                   trial.selected[i],
                                                   UniformBackend::Auto,
                                                   &trial.stats[i],
                                                   options);
        }
      } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!error) {
          error = std::current_exception();
        }
      }
    }
  }
  trial.seconds
      = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
  if (error) {
    std::rethrow_exception(error);
  }
  return trial;
}

TEST(UniformBitmapTest, BenchmarkExported2x1)
{
  const char* path = std::getenv("FIN_UNIFORM_BITMAP_INPUT");
  if (!path) {
    GTEST_SKIP() << "Set FIN_UNIFORM_BITMAP_INPUT to exported TPU 2x1 CSVs.";
  }
  std::ifstream tiles(std::string(path) + "/tiles.csv");
  ASSERT_TRUE(tiles.good());
  std::string line;
  std::getline(tiles, line);
  std::vector<double> targets;
  while (std::getline(tiles, line)) {
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream row(line);
    size_t tile;
    double x1, y1, x2, y2, metal, target, capacity;
    ASSERT_TRUE(static_cast<bool>(row >> tile >> x1 >> y1 >> x2 >> y2 >> metal
                                  >> target >> capacity));
    ASSERT_EQ(tile, targets.size());
    targets.push_back(target);
  }
  ASSERT_FALSE(targets.empty());
  std::vector<std::vector<Rectangle>> candidates(targets.size());
  std::ifstream sites(std::string(path) + "/candidates.csv");
  ASSERT_TRUE(sites.good());
  std::getline(sites, line);
  while (std::getline(sites, line)) {
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream row(line);
    size_t tile;
    int x1, y1, x2, y2;
    ASSERT_TRUE(static_cast<bool>(row >> tile >> x1 >> y1 >> x2 >> y2));
    ASSERT_LT(tile, candidates.size());
    candidates[tile].emplace_back(x1, y1, x2, y2);
  }
  for (int threads : {4, 32}) {
    // Warm greedy, hash, old bitmap, each optimization and their combination.
    for (int mode = 0; mode < 7; ++mode) {
      runSelection(candidates, targets, threads, mode);
    }
    for (int repeat = 0; repeat < 7; ++repeat) {
      std::array<Trial, 7> trials;
      // Rotate policy order; each policy receives the same input and targets.
      for (int offset = 0; offset < 7; ++offset) {
        const int mode = (repeat + offset) % 7;
        trials[mode] = runSelection(candidates, targets, threads, mode);
      }
      const auto& greedy = trials[0];
      const auto& before = trials[1];
      const auto& after = trials[6];
      for (int mode = 2; mode < 7; ++mode) {
        ASSERT_TRUE(before.selected == trials[mode].selected) << mode;
        ASSERT_EQ(before.areas, trials[mode].areas) << mode;
      }
      size_t bitmap = 0, hash = 0, small = 0, count = 0, greedy_count = 0;
      for (size_t i = 0; i < candidates.size(); ++i) {
        bitmap += after.stats[i].bitmap_groups;
        hash += after.stats[i].hash_groups;
        small += after.stats[i].small_groups;
        count += after.selected[i].size();
        greedy_count += greedy.selected[i].size();
        ASSERT_LE(greedy.areas[i], targets[i]);
      }
      std::cout << "UNIFORM_BITMAP_BENCH: threads=" << threads
                << " actual=" << after.actual_threads << " repeat=" << repeat
                << " greedy_seconds=" << greedy.seconds
                << " greedy_selected=" << greedy_count
                << " hash_seconds=" << before.seconds
                << " bitmap_seconds=" << trials[2].seconds
                << " small_seconds=" << trials[3].seconds
                << " reuse_seconds=" << trials[4].seconds
                << " lookup_seconds=" << trials[5].seconds
                << " optimized_seconds=" << after.seconds
                << " selected=" << count << " bitmap_groups=" << bitmap
                << " hash_groups=" << hash << " small_groups=" << small
                << std::endl;
    }
  }
}
}  // namespace
}  // namespace fin::lpfill

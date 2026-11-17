// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "CandidateSelection.h"
#include "FillGeometry.h"
#include "LPFillUtil.h"
#include "MinFillAmountLp.h"
#include "TileGrid.h"
#include "gtest/gtest.h"

namespace fin::lpfill {
namespace {
void writeRectangles(const std::string& filename,
                     const std::vector<Rectangle>& rectangles)
{
  std::ofstream out(filename);
  out << "x1,y1,x2,y2\n";
  for (const auto& rect : rectangles) {
    out << xl(rect) << ',' << yl(rect) << ',' << xh(rect) << ',' << yh(rect)
        << '\n';
  }
  if (!out.good()) {
    throw std::runtime_error("Cannot write comparison geometry: " + filename);
  }
}

std::vector<double> exportComparisonInput(const std::string& directory,
                                          int dbu,
                                          const TileGrid& grid,
                                          const LayerFillCandidates& candidates,
                                          const std::vector<Rectangle>& metal)
{
  FixedDissectionLpProblem problem;
  problem.lp_solver = FillLpSolver::Pdlp;
  problem.lp_threads = 4;
  problem.min_tile_density = .2;
  problem.max_tile_density = 1;
  problem.min_window_density = .3;
  problem.max_window_density = .6;
  problem.feature_areas = grid.metalAreas();
  problem.windows = grid.windowTileIndices();
  size_t count = 0;
  for (size_t i = 0; i < candidates.tiles.size(); ++i) {
    problem.tile_areas.push_back(grid.tiles()[i].area());
    double capacity = 0;
    for (const auto& rect : candidates.tiles[i]) {
      capacity += boost::polygon::area(rect);
    }
    problem.max_fill_areas.push_back(capacity);
    count += candidates.tiles[i].size();
  }
  const auto result = solveMinFillAmountLp(problem);
  if (!result.solved) {
    throw std::runtime_error("Comparison LP failed: " + result.solver_status);
  }
  std::ofstream meta(directory + "/metadata.json");
  meta << std::setprecision(17) << "{\"dbu\":" << dbu
       << ",\"method\":\"min_amount\",\"solver\":\"PDLP\",\"lp_threads\":4"
       << ",\"placement_threads\":4,\"candidates\":" << count
       << ",\"iterations\":" << result.solver_iterations
       << ",\"lp_seconds\":" << result.solve_seconds << "}\n";
  std::ofstream tiles(directory + "/tiles.csv");
  tiles << "tile,x1,y1,x2,y2,metal_area,target_area,capacity\n"
        << std::setprecision(17);
  std::ofstream sites(directory + "/candidates.csv");
  sites << "tile,x1,y1,x2,y2\n";
  for (size_t i = 0; i < candidates.tiles.size(); ++i) {
    const auto& tile = grid.tiles()[i];
    tiles << i << ',' << tile.xMin() << ',' << tile.yMin() << ',' << tile.xMax()
          << ',' << tile.yMax() << ',' << problem.feature_areas[i] << ','
          << result.fill_areas[i] << ',' << problem.max_fill_areas[i] << '\n';
    for (const auto& rect : candidates.tiles[i]) {
      sites << i << ',' << xl(rect) << ',' << yl(rect) << ',' << xh(rect) << ','
            << yh(rect) << '\n';
    }
  }
  writeRectangles(directory + "/metal.csv", metal);
  if (!meta.good() || !tiles.good() || !sites.good()) {
    throw std::runtime_error("Cannot write comparison input.");
  }
  return result.fill_areas;
}

void exportComparisonPolicy(const std::string& directory,
                            const std::string& name,
                            const LayerFillPlacement& placement,
                            const TileGrid& density)
{
  writeRectangles(directory + "/" + name + ".csv", placement.fills);
  std::ofstream tiles(directory + "/" + name + "_tiles.csv");
  tiles << "tile,placed_area,density\n" << std::setprecision(17);
  for (size_t i = 0; i < placement.placed_areas.size(); ++i) {
    tiles << i << ',' << placement.placed_areas[i] << ','
          << density.metalDensities()[i] << '\n';
  }
  std::ofstream windows(directory + "/" + name + "_windows.csv");
  windows << "window,density\n" << std::setprecision(17);
  for (size_t i = 0; i < density.windows().size(); ++i) {
    windows << i << ',' << density.windows()[i].density << '\n';
  }
  std::ofstream timing(directory + "/" + name + "_timing.json");
  timing << std::setprecision(17) << "{\"fills\":" << placement.fills.size()
         << ",\"select_seconds\":" << placement.select_seconds
         << ",\"placement_prepare_seconds\":" << placement.wall_seconds
         << "}\n";
  if (!tiles.good() || !windows.good() || !timing.good()) {
    throw std::runtime_error("Cannot write comparison result.");
  }
}

std::vector<Rectangle> regularGrid(int size, int offset = 0)
{
  std::vector<Rectangle> result;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      result.emplace_back(offset + x * 4,
                          offset + y * 4,
                          offset + x * 4 + 2,
                          offset + y * 4 + 2);
    }
  }
  return result;
}

TEST(SelectionPolicyTest, DefaultRetainsGreedyOrderAndNeverExceedsTarget)
{
  const std::vector<std::vector<Rectangle>> candidates{
      {{0, 0, 2, 2}, {4, 0, 6, 2}, {8, 0, 9, 1}}};
  const auto result = selectLayerFillCandidates(candidates, {5.0}, 1);
  const auto explicit_greedy = selectLayerFillCandidates(
      candidates, {5.0}, 1, SelectionPolicy::Greedy);
  const std::vector<Rectangle> expected{candidates[0][0], candidates[0][2]};
  EXPECT_TRUE(result.fills == expected);
  EXPECT_TRUE(result.fills == explicit_greedy.fills);
  EXPECT_EQ(result.placed_areas[0], 5.0);
}

TEST(SelectionPolicyTest, UniformRoundsUpAndPreservesOriginalOutputOrder)
{
  const auto candidates = regularGrid(3);
  for (const double target : {0.0, 0.0005, 0.002, 4.0, 5.0, 36.0, 40.0}) {
    std::vector<Rectangle> selected;
    const auto area
        = selectUniformCandidates(candidates, target, 2, 2, selected);
    const auto count
        = target <= .001 ? 0 : std::min(9, int(std::ceil(target / 4)));
    EXPECT_EQ(selected.size(), count);
    EXPECT_DOUBLE_EQ(area, count * 4.0);
    size_t next = 0;
    for (const auto& rect : selected) {
      while (next < candidates.size() && candidates[next] != rect) {
        ++next;
      }
      ASSERT_LT(next, candidates.size());
      ++next;
    }
  }
}

TEST(SelectionPolicyTest, UniformPrefersInteriorAndUsesSparseParityBands)
{
  for (const int offset : {0, -17, 1000000000}) {
    const auto candidates = regularGrid(3, offset);
    std::vector<Rectangle> selected;
    EXPECT_EQ(selectUniformCandidates(candidates, 4, 2, 2, selected), 4);
    ASSERT_EQ(selected.size(), 1);
    EXPECT_TRUE(selected[0] == candidates[4]);
  }
  auto candidates = regularGrid(4);
  // Reverse the input: spatial priority must not become reverse input order.
  std::reverse(candidates.begin(), candidates.end());
  std::vector<Rectangle> selected;
  selectUniformCandidates(candidates, 8, 2, 2, selected);
  const std::vector<Rectangle> expected{{8, 8, 10, 10}, {8, 4, 10, 6}};
  EXPECT_TRUE(selected == expected);
}

TEST(SelectionPolicyTest, MultipleSizesAndShiftedGridsDoNotInventCandidates)
{
  auto candidates = regularGrid(3);
  const auto shifted = regularGrid(3, 101);
  candidates.insert(candidates.end(), shifted.begin(), shifted.end());
  candidates.emplace_back(300, 300, 301, 303);
  std::vector<Rectangle> selected;
  const auto area = selectUniformCandidates(candidates, 74, 2, 2, selected);
  EXPECT_EQ(area, 75);
  EXPECT_TRUE(selected == candidates);
}

TEST(SelectionPolicyTest, ParallelPlacementMatchesSerialAndGeometricArea)
{
  std::vector<std::vector<Rectangle>> tiles;
  std::vector<double> targets;
  for (int i = 0; i < 40; ++i) {
    tiles.push_back(regularGrid(5, i * 100));
    targets.push_back(4 * (i % 25) + 0.5);
  }
  for (const auto policy :
       {SelectionPolicy::Greedy, SelectionPolicy::Uniform}) {
    const auto serial
        = selectLayerFillCandidates(tiles, targets, 1, policy, 2, 2);
    const auto parallel
        = selectLayerFillCandidates(tiles, targets, 4, policy, 2, 2);
    EXPECT_TRUE(serial.fills == parallel.fills);
    EXPECT_EQ(serial.placed_areas, parallel.placed_areas);
    double sum = 0;
    for (const auto area : parallel.placed_areas) {
      sum += area;
    }
    EXPECT_DOUBLE_EQ(boost::polygon::area(parallel.selected_shapes), sum);
  }
}

TEST(SelectionPolicyTest, RejectsInvalidPolicyAndUniformGeometry)
{
  EXPECT_EQ(parseSelectionPolicy("greedy"), SelectionPolicy::Greedy);
  EXPECT_EQ(parseSelectionPolicy("uniform"), SelectionPolicy::Uniform);
  EXPECT_THROW(parseSelectionPolicy("blaze"), std::invalid_argument);
  const std::vector<Rectangle> duplicates{{0, 0, 2, 2}, {0, 0, 2, 2}};
  std::vector<Rectangle> selected;
  EXPECT_THROW(selectUniformCandidates(duplicates, 4, 2, 2, selected),
               std::invalid_argument);
  EXPECT_THROW(selectUniformCandidates({}, -1, 2, 2, selected),
               std::invalid_argument);
  EXPECT_THROW(
      selectUniformCandidates(
          {}, std::numeric_limits<double>::quiet_NaN(), 2, 2, selected),
      std::invalid_argument);
}

TEST(SelectionPolicyTest, VerifyExistingSmallOdb)
{
  const char* path = std::getenv("FIN_SELECTION_TEST_ODB");
  if (path == nullptr) {
    GTEST_SKIP()
        << "Set FIN_SELECTION_TEST_ODB for real candidate/density validation.";
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
  FillShapesConfig config{{{int(.16 * dbu), int(.16 * dbu)},
                           {int(.08 * dbu), int(.08 * dbu)},
                           {int(.04 * dbu), int(.04 * dbu)}},
                          spacing,
                          spacing,
                          0};
  if (const char* offset = std::getenv("FIN_SELECTION_TEST_STAGGER_OFFSET")) {
    config.pattern.type = FillPattern::Stagger;
    config.pattern.stagger_offset = std::stod(offset);
  }
  TileGrid initial({block->getDieArea(), odb::Point(0, 0), 50 * dbu, 4});
  std::vector<Rectangle> expanded, originals;
  const Polygon90Set metal = orNonFills(block,
                                        layer,
                                        nullptr,
                                        4,
                                        nullptr,
                                        nullptr,
                                        spacing,
                                        nullptr,
                                        &expanded,
                                        &originals);
  initial.calculateMetalDensitiesFromRectangles(originals, 4);
  const auto areas = makeLayerFillAreasFromRectangles(
      initial, expanded, layer, config, nullptr, 4);
  const auto candidates
      = makeLayerFillCandidates(areas.tile_areas, layer, config, nullptr, 4);
  std::vector<double> targets(candidates.tiles.size());
  for (size_t i = 0; i < targets.size(); ++i) {
    double capacity = 0;
    for (const auto& rect : candidates.tiles[i]) {
      capacity += boost::polygon::area(rect);
    }
    targets[i] = capacity * .2317;
  }
  const char* export_dir = std::getenv("FIN_SELECTION_EXPORT_DIR");
  if (export_dir != nullptr) {
    // One real LP solution is shared by both policies for a fair comparison.
    targets = exportComparisonInput(
        export_dir, dbu, initial, candidates, originals);
  }
  for (const auto policy :
       {SelectionPolicy::Greedy, SelectionPolicy::Uniform}) {
    const auto serial = selectLayerFillCandidates(
        candidates.tiles, targets, 1, policy, spacing, spacing, config.pattern);
    const auto parallel = selectLayerFillCandidates(
        candidates.tiles, targets, 4, policy, spacing, spacing, config.pattern);
    EXPECT_TRUE(serial.fills == parallel.fills);
    EXPECT_EQ(serial.placed_areas, parallel.placed_areas);
    auto incremental = initial;
    incremental.addDisjointFillAreas(parallel.placed_areas);
    auto geometry = initial;
    Polygon90Set combined = metal;
    combined += parallel.selected_shapes;
    geometry.calculateMetalDensities(combined);
    EXPECT_EQ(incremental.metalAreas(), geometry.metalAreas());
    ASSERT_EQ(incremental.windows().size(), geometry.windows().size());
    for (size_t i = 0; i < incremental.windows().size(); ++i) {
      EXPECT_DOUBLE_EQ(incremental.windows()[i].metal_area,
                       geometry.windows()[i].metal_area);
    }
    if (export_dir != nullptr) {
      exportComparisonPolicy(
          export_dir,
          policy == SelectionPolicy::Greedy ? "greedy" : "uniform",
          parallel,
          incremental);
    }
    std::cout << "SELECTION_VALIDATION: policy="
              << (policy == SelectionPolicy::Greedy ? "greedy" : "uniform")
              << " tiles=" << targets.size()
              << " fills=" << parallel.fills.size() << " pattern="
              << (config.pattern.type == FillPattern::Stagger ? "stagger"
                                                              : "grid")
              << " offset=" << config.pattern.stagger_offset
              << " geometry_and_threads_match=1" << std::endl;
  }
}
}  // namespace
}  // namespace fin::lpfill

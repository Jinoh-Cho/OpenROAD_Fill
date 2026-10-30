// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#include "DensityAnalyzer.h"
#include "FillSvgWriter.h"
#include "LPFillUtil.h"
#include "TileGrid.h"
#include "gtest/gtest.h"

namespace fin {
namespace {

class LPFillUtilTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    db_ = odb::dbDatabase::create();
    auto* tech = odb::dbTech::create(db_, "tech");
    layer_
        = odb::dbTechLayer::create(tech, "M2", odb::dbTechLayerType::ROUTING);
    layer_->setDirection(odb::dbTechLayerDir::HORIZONTAL);
  }
  void TearDown() override { odb::dbDatabase::destroy(db_); }

  odb::dbDatabase* db_ = nullptr;
  odb::dbTechLayer* layer_ = nullptr;
};

TEST_F(LPFillUtilTest, SpacingFollowsLayerDirection)
{
  const FillShapesConfig config{{{2, 2}}, 1, 1, 3};
  EXPECT_EQ(lpfill::getSpacing(layer_, config), std::make_pair(3, 1));
  layer_->setDirection(odb::dbTechLayerDir::VERTICAL);
  EXPECT_EQ(lpfill::getSpacing(layer_, config), std::make_pair(1, 3));
}

TEST_F(LPFillUtilTest, ParallelPlacementPreservesGreedySelectionAndGeometry)
{
  std::vector<std::vector<Rectangle>> candidates;
  std::vector<double> targets;
  std::vector<double> expected_areas;
  std::vector<Rectangle> expected_fills;
  Polygon90Set expected_shapes;
  // The 4-area target must skip the first (8-area) candidate, then take
  // exactly one later candidate. Zero targets must select nothing.
  for (int tile = 0; tile < 128; ++tile) {
    const int x = tile * 20;
    const Rectangle first(x, 0, x + 4, 2);
    const Rectangle second(x + 6, 0, x + 8, 2);
    const Rectangle third(x + 10, 0, x + 12, 2);
    candidates.push_back({first, second, third});
    const double target = tile % 3 == 0 ? 12.0 : (tile % 3 == 1 ? 4.0 : 0.0);
    targets.push_back(target);
    expected_areas.push_back(target);
    if (tile % 3 == 0) {
      expected_fills.push_back(first);
      expected_shapes.insert(first);
    }
    if (tile % 3 != 2) {
      expected_fills.push_back(second);
      expected_shapes.insert(second);
    }
  }
  for (const int threads : {1, 2, 8}) {
    const auto placed
        = lpfill::selectLayerFillCandidates(candidates, targets, threads);
    EXPECT_EQ(placed.fills, expected_fills);
    EXPECT_EQ(placed.placed_areas, expected_areas);
    EXPECT_GE(placed.select_seconds, 0.0);
    EXPECT_LE(placed.select_seconds, placed.wall_seconds);
    EXPECT_GE(placed.select_work_seconds, 0.0);
    EXPECT_LE(placed.select_work_seconds, placed.work_seconds);
    double phase_total = 0.0;
    for (const auto& [name, value] : placed.phase_seconds) {
      EXPECT_GE(value, 0.0) << name;
      phase_total += value;
    }
    EXPECT_NEAR(phase_total, placed.wall_seconds, 1e-9);
    EXPECT_DOUBLE_EQ(placed.phase_seconds.at("placement_select_seconds"),
                     placed.select_seconds);
    Polygon90Set missing = expected_shapes - placed.selected_shapes;
    Polygon90Set extra = placed.selected_shapes - expected_shapes;
    EXPECT_EQ(boost::polygon::area(missing), 0);
    EXPECT_EQ(boost::polygon::area(extra), 0);
    EXPECT_GE(placed.threads, 1);
    EXPECT_LE(placed.threads, threads);
  }
}

TEST_F(LPFillUtilTest, PlacementHandlesEmptyTilesAndRejectsInvalidArguments)
{
  const auto empty = lpfill::selectLayerFillCandidates({}, {}, 8);
  EXPECT_TRUE(empty.fills.empty());
  EXPECT_TRUE(empty.placed_areas.empty());
  EXPECT_EQ(empty.threads, 1);
  const auto blocked = lpfill::selectLayerFillCandidates(
      {{Rectangle(0, 0, 2, 2)}, {}}, {3.0, 5.0}, 8);
  EXPECT_TRUE(blocked.fills.empty());
  EXPECT_EQ(blocked.placed_areas, (std::vector<double>{0.0, 0.0}));
  EXPECT_THROW(lpfill::selectLayerFillCandidates({{}}, {}, 1),
               std::invalid_argument);
  EXPECT_THROW(lpfill::selectLayerFillCandidates({}, {}, 0),
               std::invalid_argument);
}

TEST_F(LPFillUtilTest, DensityProfilingPreservesOverlapAndWindowAreas)
{
  const TileGridConfig config{odb::Rect(0, 0, 40, 40), odb::Point(0, 0), 20, 2};
  TileGrid baseline(config);
  TileGrid profiled(config);
  Polygon90Set shapes;
  shapes.insert(Rectangle(0, 0, 20, 20));
  shapes.insert(Rectangle(10, 10, 30, 30));
  baseline.calculateMetalDensities(shapes);
  std::map<std::string, double> phases;
  profiled.calculateMetalDensities(shapes, &phases);
  EXPECT_DOUBLE_EQ(profiled.totalMetalArea(), 700.0);
  EXPECT_EQ(profiled.metalAreas(), baseline.metalAreas());
  EXPECT_EQ(profiled.metalDensities(), baseline.metalDensities());
  ASSERT_EQ(profiled.windows().size(), baseline.windows().size());
  for (size_t index = 0; index < profiled.windows().size(); ++index) {
    EXPECT_DOUBLE_EQ(profiled.windows()[index].density,
                     baseline.windows()[index].density);
  }
  for (const char* phase : {"post_fill_density_setup_seconds",
                            "post_fill_union_clip_seconds",
                            "post_fill_decompose_seconds",
                            "post_fill_tile_accumulation_seconds",
                            "post_fill_window_seconds"}) {
    EXPECT_GE(phases.at(phase), 0.0);
  }
}

TEST_F(LPFillUtilTest, TileCandidatesUsePolygonCandidatesAndRespectObstacles)
{
  const FillShapesConfig config{{{2, 2}}, 1, 1, 1};
  Polygon90Set obstacles;
  obstacles.insert(boost::polygon::rectangle_data<int>(8, 8, 12, 12));
  Polygon90Set fillable;
  const auto candidates = lpfill::makeTileFillCandidates(
      odb::Rect(0, 0, 20, 20), obstacles, layer_, config, nullptr, &fillable);
  ASSERT_FALSE(candidates.empty());

  std::vector<Polygon90> polygons;
  fillable.get(polygons);
  std::vector<Rectangle> expected;
  for (const auto& polygon : polygons) {
    const auto partial
        = lpfill::makeFillCandidates(polygon, layer_, config, nullptr);
    expected.insert(expected.end(), partial.begin(), partial.end());
  }
  EXPECT_EQ(candidates, expected);
  for (const auto& candidate : candidates) {
    EXPECT_GE(xl(candidate), 1);
    EXPECT_GE(yl(candidate), 1);
    EXPECT_LE(xh(candidate), 19);
    EXPECT_LE(yh(candidate), 19);
    EXPECT_TRUE(xh(candidate) <= 7 || xl(candidate) >= 13 || yh(candidate) <= 7
                || yl(candidate) >= 13);
  }
}

TEST_F(LPFillUtilTest, LayerFillAreasMatchIndependentTileGeometry)
{
  const std::vector<TileGridConfig> grids{
      {odb::Rect(0, 0, 40, 40), odb::Point(0, 0), 20, 4},
      {odb::Rect(-13, -17, 61, 49), odb::Point(1, 2), 20, 4},
      {odb::Rect(2, 3, 5, 7), odb::Point(0, 0), 20, 4}};
  const FillShapesConfig config{{{4, 2}, {2, 2}}, 1, 2, 3};
  for (const auto direction :
       {odb::dbTechLayerDir::HORIZONTAL, odb::dbTechLayerDir::VERTICAL}) {
    layer_->setDirection(direction);
    for (const auto& grid_config : grids) {
      const TileGrid grid(grid_config);
      for (int obstacle_case = 0; obstacle_case < 4; obstacle_case++) {
        Polygon90Set obstacles;
        if (obstacle_case == 1) {
          // Includes holes, overlaps, tile crossings and an outside obstacle
          // whose spacing halo reaches the grid's edge.
          for (const Rectangle& rectangle : {Rectangle(-30, -30, -12, 70),
                                             Rectangle(-5, -5, 23, 7),
                                             Rectangle(10, -10, 15, 35),
                                             Rectangle(20, 10, 45, 15),
                                             Rectangle(20, 25, 45, 30),
                                             Rectangle(20, 10, 25, 30),
                                             Rectangle(40, 10, 45, 30)}) {
            obstacles.insert(rectangle);
          }
        } else if (obstacle_case == 2) {
          obstacles.insert(Rectangle(-100, -100, 100, 100));
        } else if (obstacle_case == 3) {
          std::mt19937 generator(12345);
          std::uniform_int_distribution<int> coordinate(-25, 65);
          std::uniform_int_distribution<int> length(1, 12);
          for (int index = 0; index < 40; index++) {
            const int x = coordinate(generator);
            const int y = coordinate(generator);
            obstacles.insert(
                Rectangle(x, y, x + length(generator), y + length(generator)));
          }
        }
        const Polygon90Set bloated = obstacles + config.space_to_non_fill;
        const auto prepared = lpfill::makeLayerFillAreas(
            grid, bloated, layer_, config, nullptr);
        const auto parallel_prepared = lpfill::makeLayerFillAreas(
            grid, bloated, layer_, config, nullptr, 16);
        EXPECT_EQ(prepared.threads, 1);
        EXPECT_GE(parallel_prepared.threads, 1);
        EXPECT_LE(parallel_prepared.threads, 16);
        EXPECT_EQ(parallel_prepared.bloated_non_fill_areas,
                  prepared.bloated_non_fill_areas);
        const auto parallel_prepared_candidates
            = lpfill::makeLayerFillCandidates(
                parallel_prepared.tile_areas, layer_, config, nullptr, 4);
        ASSERT_EQ(prepared.tile_areas.size(), grid.tiles().size());
        ASSERT_EQ(prepared.bloated_non_fill_areas.size(), grid.tiles().size());
        const auto parallel = lpfill::makeLayerFillCandidates(
            prepared.tile_areas, layer_, config, nullptr, 4);
        const auto serial = lpfill::makeLayerFillCandidates(
            prepared.tile_areas, layer_, config, nullptr, 1);
        ASSERT_EQ(parallel.tiles, serial.tiles);
        EXPECT_EQ(parallel_prepared_candidates.tiles, serial.tiles);
        EXPECT_EQ(serial.threads, 1);
        EXPECT_GT(parallel.wall_seconds, 0.0);
        EXPECT_GT(parallel.work_seconds, 0.0);
        for (size_t index = 0; index < grid.tiles().size(); index++) {
          SCOPED_TRACE(testing::Message()
                       << "tile=" << index
                       << " obstacle_case=" << obstacle_case);
          const auto& tile = grid.tiles()[index];
          Polygon90Set expected_area;
          const auto expected = lpfill::makeTileFillCandidates(
              tile, obstacles, layer_, config, nullptr, &expected_area);
          EXPECT_EQ(parallel.tiles[index], expected);
          Polygon90Set parallel_missing = prepared.tile_areas[index]
                                          - parallel_prepared.tile_areas[index];
          Polygon90Set parallel_extra = parallel_prepared.tile_areas[index]
                                        - prepared.tile_areas[index];
          EXPECT_EQ(boost::polygon::area(parallel_missing), 0);
          EXPECT_EQ(boost::polygon::area(parallel_extra), 0);
          Polygon90Set missing = expected_area - prepared.tile_areas[index];
          Polygon90Set extra = prepared.tile_areas[index] - expected_area;
          EXPECT_EQ(boost::polygon::area(missing), 0);
          EXPECT_EQ(boost::polygon::area(extra), 0);
          EXPECT_EQ(lpfill::makeTileFillCandidates(
                        prepared.tile_areas[index], layer_, config, nullptr),
                    expected);
          Polygon90Set tile_polygon;
          tile_polygon.insert(
              Rectangle(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax()));
          EXPECT_DOUBLE_EQ(prepared.bloated_non_fill_areas[index],
                           boost::polygon::area(bloated & tile_polygon));
        }
      }
    }
  }
}

TEST_F(LPFillUtilTest, LayerFillAreasRejectInvalidThreads)
{
  const TileGrid grid({odb::Rect(0, 0, 20, 20), odb::Point(0, 0), 20, 4});
  const FillShapesConfig config{{{4, 2}}, 1, 2, 3};
  const Polygon90Set obstacles;
  EXPECT_THROW(
      lpfill::makeLayerFillAreas(grid, obstacles, layer_, config, nullptr, 0),
      std::invalid_argument);
  EXPECT_THROW(
      lpfill::makeLayerFillAreas(grid, obstacles, layer_, config, nullptr, -1),
      std::invalid_argument);
}

TEST_F(LPFillUtilTest, CandidateGenerationHandlesEmptyInputAndInvalidThreads)
{
  const FillShapesConfig config{{{4, 2}}, 1, 2, 3};
  const auto empty
      = lpfill::makeLayerFillCandidates({}, layer_, config, nullptr, 4);
  EXPECT_TRUE(empty.tiles.empty());
  EXPECT_EQ(empty.threads, 1);
  EXPECT_DOUBLE_EQ(empty.wall_seconds, 0.0);
  EXPECT_THROW(lpfill::makeLayerFillCandidates({}, layer_, config, nullptr, 0),
               std::invalid_argument);
}

double windowDensity(const std::vector<odb::Rect>& rectangles,
                     const odb::Rect& window)
{
  double area = 0.0;
  for (const odb::Rect& rectangle : rectangles) {
    const int overlap_x
        = std::max(0,
                   std::min(rectangle.xMax(), window.xMax())
                       - std::max(rectangle.xMin(), window.xMin()));
    const int overlap_y
        = std::max(0,
                   std::min(rectangle.yMax(), window.yMax())
                       - std::max(rectangle.yMin(), window.yMin()));
    area += static_cast<double>(overlap_x) * overlap_y;
  }
  return area / window.area();
}

TEST(FillUtillTest, Alg2MatchesBruteForceExtrema)
{
  const odb::Rect region(0, 0, 10, 10);
  const int window_size = 4;
  const std::vector<odb::Rect> rectangles{
      odb::Rect(0, 0, 2, 7), odb::Rect(2, 5, 6, 9), odb::Rect(7, 1, 10, 4)};

  double expected_min = 1.0;
  double expected_max = 0.0;
  for (int y = region.yMin(); y + window_size <= region.yMax(); y++) {
    for (int x = region.xMin(); x + window_size <= region.xMax(); x++) {
      const double density = windowDensity(
          rectangles, odb::Rect(x, y, x + window_size, y + window_size));
      expected_min = std::min(expected_min, density);
      expected_max = std::max(expected_max, density);
    }
  }

  const FloatingDensityResult result
      = analyzeFloatingDensityAlg2(rectangles, region, window_size);
  const FloatingDensityResult alg3_result
      = analyzeFloatingDensityAlg3(rectangles, region, window_size);

  EXPECT_NEAR(result.min_density, expected_min, 1e-9);
  EXPECT_NEAR(result.max_density, expected_max, 1e-9);
  EXPECT_NEAR(windowDensity(rectangles, result.min_window), expected_min, 1e-9);
  EXPECT_NEAR(windowDensity(rectangles, result.max_window), expected_max, 1e-9);
  EXPECT_NEAR(alg3_result.min_density, result.min_density, 1e-9);
  EXPECT_NEAR(alg3_result.max_density, result.max_density, 1e-9);
  EXPECT_NEAR(
      windowDensity(rectangles, alg3_result.min_window), expected_min, 1e-9);
  EXPECT_NEAR(
      windowDensity(rectangles, alg3_result.max_window), expected_max, 1e-9);
  EXPECT_LE(result.profiles.size(), 50);
  EXPECT_FALSE(result.profiles.empty());
  for (const FloatingDensityResult::Profile& profile : result.profiles) {
    EXPECT_FALSE(profile.points.empty());
    EXPECT_EQ(profile.points.front().first, region.xMin());
    EXPECT_EQ(profile.points.back().first, region.xMax() - window_size);
  }
  for (const FloatingDensityResult::Profile& profile : alg3_result.profiles) {
    ASSERT_FALSE(profile.points.empty());
    EXPECT_EQ(profile.points.front().first, region.xMin());
    EXPECT_EQ(profile.points.back().first, region.xMax() - window_size);
  }
}

TEST(FillUtillTest, Alg2RejectsWindowLargerThanRegion)
{
  EXPECT_THROW(analyzeFloatingDensityAlg2({}, odb::Rect(0, 0, 10, 10), 11),
               std::invalid_argument);
}

TEST(FillUtillTest, Alg3RejectsWindowLargerThanRegion)
{
  EXPECT_THROW(analyzeFloatingDensityAlg3({}, odb::Rect(0, 0, 10, 10), 11),
               std::invalid_argument);
}

TEST(FillUtillTest, SharedSummaryAndHistogram)
{
  std::vector<DensityWindow> windows(3);
  windows[0].density = 0.0;
  windows[1].density = 0.5;
  windows[2].density = 1.0;
  const auto summary = summarizeWindowDensities(windows, 0.1, 0.9);
  EXPECT_DOUBLE_EQ(summary.min_density, 0.0);
  EXPECT_DOUBLE_EQ(summary.max_density, 1.0);
  EXPECT_DOUBLE_EQ(summary.mean_density, 0.5);
  EXPECT_NEAR(summary.variance, 1.0 / 6.0, 1e-12);
  EXPECT_EQ(summary.min_violation_count, 1);
  EXPECT_EQ(summary.max_violation_count, 1);
  const auto histogram = makeWindowDensityHistogram(windows);
  EXPECT_EQ(histogram[0], 1);
  EXPECT_EQ(histogram[10], 1);
  EXPECT_EQ(histogram[19], 1);
  EXPECT_DOUBLE_EQ(summarizeWindowDensities({}, 0.1, 0.9).mean_density, 0.0);
}

TEST(FillUtillTest, GridAnalysisCountsOverlappingMetalOnce)
{
  TileGrid grid({odb::Rect(0, 0, 10, 10), odb::Point(0, 0), 10, 2});
  Polygon90Set metal;
  metal.insert(boost::polygon::rectangle_data<int>(0, 0, 5, 10));
  metal.insert(boost::polygon::rectangle_data<int>(0, 0, 5, 5));
  grid.calculateMetalDensities(metal);
  EXPECT_EQ(grid.tiles().size(), 4);
  EXPECT_DOUBLE_EQ(grid.totalMetalArea(), 50.0);
  ASSERT_EQ(grid.windows().size(), 1);
  EXPECT_DOUBLE_EQ(grid.windows()[0].density, 0.5);
}

TEST(FillUtillTest, GridDensityPartitionMatchesIndependentIntersections)
{
  const std::vector<TileGridConfig> configs{
      {odb::Rect(0, 0, 40, 40), odb::Point(0, 0), 20, 4},
      {odb::Rect(-13, -17, 61, 49), odb::Point(1, 2), 20, 4},
      {odb::Rect(2, 3, 5, 7), odb::Point(0, 0), 20, 4},
      {odb::Rect(0, 0, 0, 0), odb::Point(0, 0), 20, 4}};
  for (const auto& config : configs) {
    TileGrid grid(config);
    for (int metal_case = 0; metal_case < 5; metal_case++) {
      Polygon90Set metal;
      if (metal_case == 1) {
        metal.insert(Rectangle(-100, -100, 100, 100));
      } else if (metal_case == 2) {
        // Holes, duplicates, overlaps, shared boundaries, and outside metal.
        for (const auto& rectangle : {Rectangle(-20, -20, 50, -5),
                                      Rectangle(-20, 15, 50, 30),
                                      Rectangle(-20, -20, 0, 30),
                                      Rectangle(30, -20, 50, 30),
                                      Rectangle(30, -20, 50, 30),
                                      Rectangle(5, 5, 10, 10),
                                      Rectangle(10, 5, 15, 10),
                                      Rectangle(100, 100, 200, 200)}) {
          metal.insert(rectangle);
        }
      } else if (metal_case >= 3) {
        std::mt19937 generator(12345 + metal_case);
        std::uniform_int_distribution<int> coordinate(-30, 70);
        std::uniform_int_distribution<int> length(1, 25);
        for (int index = 0; index < 100; index++) {
          const int x = coordinate(generator);
          const int y = coordinate(generator);
          metal.insert(
              Rectangle(x, y, x + length(generator), y + length(generator)));
        }
        if (metal_case == 4) {
          Polygon90Set hole;
          hole.insert(Rectangle(-3, -7, 28, 33));
          metal -= hole;
        }
      }

      // Reuse the grid across cases to check that previous areas are cleared.
      grid.calculateMetalDensities(metal);
      std::vector<double> expected_areas;
      for (size_t index = 0; index < grid.tiles().size(); index++) {
        SCOPED_TRACE(testing::Message()
                     << "tile=" << index << " metal_case=" << metal_case);
        const auto& tile = grid.tiles()[index];
        Polygon90Set tile_polygon;
        tile_polygon.insert(
            Rectangle(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax()));
        const Polygon90Set covered = metal & tile_polygon;
        const double expected = boost::polygon::area(covered);
        expected_areas.push_back(expected);
        EXPECT_DOUBLE_EQ(grid.metalAreas()[index], expected);
        EXPECT_DOUBLE_EQ(grid.metalDensities()[index], expected / tile.area());
      }
      EXPECT_DOUBLE_EQ(
          grid.totalMetalArea(),
          std::accumulate(expected_areas.begin(), expected_areas.end(), 0.0));
      for (const auto& window : grid.windows()) {
        Polygon90Set window_polygon;
        window_polygon.insert(Rectangle(window.bounds.xMin(),
                                        window.bounds.yMin(),
                                        window.bounds.xMax(),
                                        window.bounds.yMax()));
        const Polygon90Set covered = metal & window_polygon;
        const double expected = boost::polygon::area(covered);
        EXPECT_DOUBLE_EQ(window.metal_area, expected);
        EXPECT_DOUBLE_EQ(window.density, expected / window.bounds.area());
      }
    }
  }
}

TEST(FillUtillTest, SvgWriterDoesNotChangeDensityAnalysis)
{
  TileGrid grid({odb::Rect(0, 0, 10, 10), odb::Point(0, 0), 10, 2});
  Polygon90Set metal;
  metal.insert(boost::polygon::rectangle_data<int>(0, 0, 5, 10));
  grid.calculateMetalDensities(metal);
  const auto filename
      = std::filesystem::temp_directory_path()
        / ("fin_density_svg_"
           + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count())
           + ".svg");
  EXPECT_TRUE(writeTileGridSvg(grid, filename.string(), metal, 1));
  std::ifstream svg(filename);
  const std::string contents{std::istreambuf_iterator<char>(svg),
                             std::istreambuf_iterator<char>()};
  EXPECT_NE(contents.find("<svg"), std::string::npos);
  EXPECT_NE(contents.find("</svg>"), std::string::npos);
  EXPECT_DOUBLE_EQ(grid.windows()[0].density, 0.5);
  EXPECT_DOUBLE_EQ(grid.totalMetalArea(), 50.0);
  svg.close();
  std::filesystem::remove(filename);
}

}  // namespace
}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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

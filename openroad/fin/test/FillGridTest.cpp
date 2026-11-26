// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "boost/json.hpp"
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

class DiscreteDensityCheckTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    db_.reset(odb::dbDatabase::create());
    auto* tech = odb::dbTech::create(db_.get(), "tech");
    db_->setDbuPerMicron(1000);
    tech->setManufacturingGrid(5);
    layer_
        = odb::dbTechLayer::create(tech, "M2", odb::dbTechLayerType::ROUTING);
    odb::dbTechLayer::create(tech, "M3", odb::dbTechLayerType::ROUTING);
    odb::dbTechLayer::create(tech, "VIA2", odb::dbTechLayerType::CUT);
    auto* chip = odb::dbChip::create(db_.get(), tech);
    block_ = odb::dbBlock::create(chip, "density_test");
    odb::dbFill::create(block_, false, 0, layer_, 0, 0, 20000, 20000);
    odb::dbFill::create(block_, false, 0, layer_, 10000, 0, 30000, 20000);
    options_.has_area = true;
    options_.area = odb::Rect(0, 0, 100000, 100000);
    options_.window_size = 50;
    options_.resolution = 5;
    options_.num_threads = 2;
    options_.report
        = (::testing::TempDir() + std::string("fin_density_check.json"));
  }

  void TearDown() override { std::filesystem::remove(options_.report); }

  boost::json::object readReport() const
  {
    std::ifstream input(options_.report);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    return boost::json::parse(contents).as_object();
  }

  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)> db_{
      nullptr,
      &odb::dbDatabase::destroy};
  odb::dbBlock* block_ = nullptr;
  odb::dbTechLayer* layer_ = nullptr;
  utl::Logger logger_;
  DiscreteDensityCheckOptions options_;
};

TEST_F(DiscreteDensityCheckTest, ReportsUnionDensityAndDoesNotModifyFills)
{
  options_.layers = {"M2", "M2"};
  const auto before = block_->getFills().size();
  checkDiscreteDensity(db_.get(), &logger_, options_);
  EXPECT_EQ(block_->getFills().size(), before);
  const auto report = readReport();
  EXPECT_DOUBLE_EQ(report.at("window_size_um").as_double(), 50.0);
  const auto& layers = report.at("layers").as_array();
  ASSERT_EQ(layers.size(), 1);
  const auto& layer = layers.front().as_object();
  EXPECT_EQ(layer.at("name").as_string(), "M2");
  EXPECT_EQ(layer.at("window_count").as_int64(), 36);
  EXPECT_DOUBLE_EQ(layer.at("min_density").as_double(), 0.0);
  EXPECT_DOUBLE_EQ(layer.at("max_density").as_double(), 0.24);
  const auto& windows = layer.at("windows").as_array();
  EXPECT_DOUBLE_EQ(windows.front().as_object().at("density").as_double(), 0.24);
  EXPECT_TRUE(windows.back().as_object().at("below_minimum").as_bool());
  EXPECT_FALSE(windows.front().as_object().at("above_maximum").as_bool());
}

TEST_F(DiscreteDensityCheckTest, DefaultsToRoutingLayersAndHonorsCheckArea)
{
  options_.window_size = 25;
  options_.area = odb::Rect(50000, 50000, 100000, 100000);
  checkDiscreteDensity(db_.get(), &logger_, options_);
  const auto report = readReport();
  const auto& bounds = report.at("area_um").as_array();
  EXPECT_DOUBLE_EQ(bounds.front().as_double(), 50.0);
  const auto& layers = report.at("layers").as_array();
  ASSERT_EQ(layers.size(), 2);
  for (const auto& value : layers) {
    EXPECT_DOUBLE_EQ(value.as_object().at("max_density").as_double(), 0.0);
  }
}

TEST_F(DiscreteDensityCheckTest, UsesManufacturingGridWindowAdjustment)
{
  options_.layers = {"M3"};
  options_.resolution = 3;
  checkDiscreteDensity(db_.get(), &logger_, options_);
  const auto report = readReport();
  EXPECT_DOUBLE_EQ(report.at("window_size_um").as_double(), 49.995);
  EXPECT_DOUBLE_EQ(report.at("tile_size_um").as_double(), 16.665);
}

TEST_F(DiscreteDensityCheckTest, RejectsInvalidLayersAndDensityBounds)
{
  for (const auto* layer : {"unknown", "VIA2"}) {
    options_.layers = {layer};
    EXPECT_ANY_THROW(checkDiscreteDensity(db_.get(), &logger_, options_));
  }
  options_.layers = {"M2"};
  options_.min_density = 0.8;
  options_.max_density = 0.2;
  EXPECT_THROW(checkDiscreteDensity(db_.get(), &logger_, options_),
               std::invalid_argument);
  EXPECT_EQ(block_->getFills().size(), 2);
}

double densityAtWindow(const Polygon90Set& metal, const odb::Rect& window)
{
  Polygon90Set bounds;
  bounds.insert(
      Rectangle(window.xMin(), window.yMin(), window.xMax(), window.yMax()));
  const Polygon90Set overlap = metal & bounds;
  return static_cast<double>(boost::polygon::area(overlap)) / window.area();
}

void expectContinuousMatchesBruteForce(const Polygon90Set& metal,
                                       const odb::Rect& area,
                                       const int window_size)
{
  double expected_minimum = 1.0;
  double expected_maximum = 0.0;
  for (int y = area.yMin(); y <= area.yMax() - window_size; ++y) {
    for (int x = area.xMin(); x <= area.xMax() - window_size; ++x) {
      const double density = densityAtWindow(
          metal, odb::Rect(x, y, x + window_size, y + window_size));
      expected_minimum = std::min(expected_minimum, density);
      expected_maximum = std::max(expected_maximum, density);
    }
  }
  std::vector<ContinuousDensityViolation> violations;
  const auto result = analyzeContinuousDensity(
      metal, area, window_size, 0.2, 0.7, [&](const auto& violation) {
        violations.push_back(violation);
      });
  EXPECT_DOUBLE_EQ(result.min_density, expected_minimum);
  EXPECT_DOUBLE_EQ(result.max_density, expected_maximum);
  EXPECT_TRUE(area.contains(result.min_window));
  EXPECT_TRUE(area.contains(result.max_window));
  EXPECT_DOUBLE_EQ(densityAtWindow(metal, result.min_window), expected_minimum);
  EXPECT_DOUBLE_EQ(densityAtWindow(metal, result.max_window), expected_maximum);
  EXPECT_EQ(violations.size(), result.below_minimum + result.above_maximum);
  EXPECT_LE(violations.size(), result.evaluated_event_windows);
  std::set<std::tuple<int, int, int, int>> seen;
  for (const auto& violation : violations) {
    EXPECT_TRUE(area.contains(violation.bounds));
    EXPECT_DOUBLE_EQ(densityAtWindow(metal, violation.bounds),
                     violation.density);
    EXPECT_EQ(violation.below_minimum, violation.density < 0.2);
    EXPECT_TRUE(violation.density < 0.2 || violation.density > 0.7);
    EXPECT_TRUE(seen.emplace(violation.bounds.xMin(),
                             violation.bounds.yMin(),
                             violation.bounds.xMax(),
                             violation.bounds.yMax())
                    .second);
  }
}

TEST(ContinuousDensityTest, MatchesBruteForceForEmptyFullAndOverlappingGeometry)
{
  const odb::Rect area(-5, -7, 7, 5);
  Polygon90Set metal;
  expectContinuousMatchesBruteForce(metal, area, 4);
  metal.insert(Rectangle(-100, -100, 100, 100));
  expectContinuousMatchesBruteForce(metal, area, 4);
  metal.clear();
  for (const Rectangle& rectangle : {Rectangle(-8, -8, -1, 1),
                                     Rectangle(-3, -4, 4, 3),
                                     Rectangle(5, -7, 9, -2)}) {
    metal.insert(rectangle);
  }
  expectContinuousMatchesBruteForce(metal, area, 4);
}

TEST(ContinuousDensityTest, MatchesBruteForceForDeterministicRandomRectangles)
{
  std::mt19937 generator(12345);
  for (int trial = 0; trial < 20; ++trial) {
    Polygon90Set metal;
    for (int i = 0; i < 10; ++i) {
      const int x = static_cast<int>(generator() % 20) - 8;
      const int y = static_cast<int>(generator() % 20) - 8;
      const int width = 1 + static_cast<int>(generator() % 6);
      const int height = 1 + static_cast<int>(generator() % 6);
      metal.insert(Rectangle(x, y, x + width, y + height));
    }
    expectContinuousMatchesBruteForce(metal, odb::Rect(-4, -3, 8, 9), 4);
  }
}

TEST(ContinuousDensityTest, HandlesCoordinateLimitsWithoutEventOverflow)
{
  for (const int origin : {std::numeric_limits<int>::min() + 1,
                           std::numeric_limits<int>::max() - 13}) {
    Polygon90Set metal;
    metal.insert(Rectangle(origin, origin, origin + 3, origin + 8));
    expectContinuousMatchesBruteForce(
        metal, odb::Rect(origin, origin, origin + 12, origin + 12), 4);
  }
}

TEST(ContinuousDensityTest,
     AllowsSinglePositionInNumericKernelAndRejectsInvalidInputs)
{
  Polygon90Set metal;
  metal.insert(Rectangle(0, 0, 2, 4));
  const odb::Rect area(0, 0, 4, 4);
  const auto result = analyzeContinuousDensity(metal, area, 4, 0, 1);
  EXPECT_DOUBLE_EQ(result.min_density, 0.5);
  EXPECT_DOUBLE_EQ(result.max_density, 0.5);
  EXPECT_EQ(result.evaluated_event_windows, 1);
  EXPECT_THROW(analyzeContinuousDensity(metal, area, 0, 0, 1),
               std::invalid_argument);
  EXPECT_THROW(analyzeContinuousDensity(metal, area, 5, 0, 1),
               std::invalid_argument);
  EXPECT_THROW(analyzeContinuousDensity(metal, area, 4, 0.8, 0.2),
               std::invalid_argument);
}

TEST_F(DiscreteDensityCheckTest,
       ContinuousReportContainsExtremaAndViolationWindows)
{
  ContinuousDensityCheckOptions options;
  options.has_area = true;
  options.area = options_.area;
  options.window_size = 50;
  options.layers = {"M2"};
  options.min_density = 0.2;
  options.max_density = 0.22;
  options.report = options_.report;
  checkContinuousDensity(db_.get(), &logger_, options);
  EXPECT_EQ(block_->getFills().size(), 2);
  const auto report = readReport();
  EXPECT_EQ(report.at("algorithm").as_string(), "ALG3");
  EXPECT_FALSE(report.at("all_continuous_positions_listed").as_bool());
  const auto& layers = report.at("layers").as_array();
  ASSERT_EQ(layers.size(), 1);
  const auto& layer = layers.front().as_object();
  EXPECT_DOUBLE_EQ(layer.at("min_density").as_double(), 0.0);
  EXPECT_DOUBLE_EQ(layer.at("max_density").as_double(), 0.24);
  EXPECT_GT(layer.at("below_minimum").as_int64(), 0);
  EXPECT_GT(layer.at("above_maximum").as_int64(), 0);
  EXPECT_EQ(layer.at("violations").as_array().size(),
            layer.at("below_minimum").as_int64()
                + layer.at("above_maximum").as_int64());
  for (const auto& value : layer.at("violations").as_array()) {
    const auto& violation = value.as_object();
    EXPECT_EQ(violation.at("bounds_um").as_array().size(), 4);
    const double density = violation.at("density").as_double();
    const bool below = violation.at("type").as_string() == "below_minimum";
    EXPECT_TRUE(below ? density < 0.2 : density > 0.22);
  }
}

}
}

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
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

}
}

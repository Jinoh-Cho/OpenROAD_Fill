// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "FillCheck.h"
#include "FillCommon.h"
#include "LPFill.h"
#include "fin/Finale.h"
#include "gtest/gtest.h"
#include "odb/db.h"
#include "tst/db_fixture.h"
#include "tst/physical_builder.h"

namespace fin {
namespace {
class FillPlacementTest : public tst::DbFixture,
                          public testing::WithParamInterface<std::string>
{
 protected:
  void SetUp() override
  {
    tech_ = tst::makeTech(getDb());
    block_ = tst::makeBlock(getDb(), tech_, odb::Rect(0, 0, 40000, 40000));
    block_->setCoreArea(odb::Rect(0, 0, 40000, 40000));
    layer_ = tst::addRoutingLayer(tech_, "M2");
    const auto tag
        = std::chrono::steady_clock::now().time_since_epoch().count();
    directory_ = std::filesystem::temp_directory_path()
                 / ("lp_fill_" + std::to_string(tag));
    ASSERT_TRUE(std::filesystem::create_directory(directory_));
    rules_ = directory_ / "fill.json";
    writeRules(false);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  void writeRules(const bool opc)
  {
    std::ofstream output(rules_);
    output << R"({"layers":{"M2":{"name":"M2","space_to_outline":0,
      "non-opc":{"datatype":0,"width":[2,1,0.5],"height":[2,1,0.5],
      "space_to_fill":0.3,"space_to_non_fill":0.5})";
    if (opc) {
      output << R"(,"opc":{"halo":1,"width":[1],"height":[1],
      "space_to_fill":0.3,"space_to_non_fill":0.5})";
    }
    output << "}}}";
  }

  void setStaggerRules(const std::string& fields)
  {
    writeRules(false);
    std::ifstream input(rules_);
    std::string contents((std::istreambuf_iterator<char>(input)), {});
    const auto position = contents.find("\"space_to_non_fill\":0.5");
    ASSERT_NE(position, std::string::npos);
    contents.insert(position + std::string("\"space_to_non_fill\":0.5").size(),
                    "," + fields);
    std::ofstream output(rules_);
    output << contents;
  }

  LPFillOptions options() const
  {
    LPFillOptions result;
    result.rules_file = rules_.string();
    result.objective = GetParam();
    result.window_width = 20;
    result.window_height = 20;
    result.step_x = 10;
    result.step_y = 10;
    result.num_threads = 4;
    return result;
  }

  odb::dbTech* tech_ = nullptr;
  odb::dbBlock* block_ = nullptr;
  odb::dbTechLayer* layer_ = nullptr;
  std::filesystem::path directory_;
  std::filesystem::path rules_;
};

TEST_P(FillPlacementTest, PreservesExistingFillAndHonorsObstructionsAndSpacing)
{
  auto* existing
      = odb::dbFill::create(block_, false, 0, layer_, 5000, 5000, 7000, 7000);
  auto* obstruction
      = odb::dbObstruction::create(block_, layer_, 20000, 0, 30000, 15000);
  obstruction->setFillObstruction();
  makeBTerm(
      block_, "input", {.bpins = {{"M2", odb::Rect(0, 18000, 1000, 22000)}}});
  LPFill filler(getDb(), getLogger());
  const auto config = options();
  ASSERT_NO_THROW(filler.fill(config));
  EXPECT_GT(block_->getFills().size(), 1);
  const FillGrid grid(block_->getCoreArea(),
                      odb::Point(20000, 20000),
                      odb::Point(10000, 10000));
  std::vector<Rectangle> rectangles;
  Polygon90Set union_area;
  Polygon90Set prohibited;
  prohibited.insert(Rectangle(19500, -500, 30500, 15500));
  prohibited.insert(Rectangle(4500, 4500, 7500, 7500));
  prohibited.insert(Rectangle(-500, 17500, 1500, 22500));
  double sum_area = 0.0;
  bool found_existing = false;
  std::vector<double> incremental(grid.tiles().size(), 0.0);
  for (auto* fill : block_->getFills()) {
    odb::Rect rect;
    fill->getRect(rect);
    const Rectangle rectangle(
        rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax());
    rectangles.push_back(rectangle);
    union_area.insert(rectangle);
    sum_area += rectangleArea(rectangle);
    if (fill == existing) {
      found_existing = true;
      EXPECT_EQ(rect, odb::Rect(5000, 5000, 7000, 7000));
      continue;
    }
    Polygon90Set shape;
    shape.insert(rectangle);
    Polygon90Set overlap = shape & prohibited;
    EXPECT_DOUBLE_EQ(boost::polygon::area(overlap), 0.0);
    bool contained = false;
    for (size_t i = 0; i < grid.tiles().size(); ++i) {
      if (grid.tiles()[i].contains(rect)) {
        incremental[i] += rectangleArea(rectangle);
        contained = true;
        break;
      }
    }
    EXPECT_TRUE(contained);
  }
  EXPECT_TRUE(found_existing);
  EXPECT_DOUBLE_EQ(boost::polygon::area(union_area), sum_area);
  // Incremental tile areas must match a fresh geometric union of actual fills.
  const auto original = grid.metalAreas({Rectangle(5000, 5000, 7000, 7000)}, 1);
  const auto full = grid.metalAreas(rectangles, 4);
  for (size_t i = 0; i < full.size(); ++i) {
    EXPECT_DOUBLE_EQ(full[i], original[i] + incremental[i]);
  }
  for (const double density : grid.windowDensities(full)) {
    EXPECT_LE(density, config.max_density + 1e-5);
  }
}

TEST_P(FillPlacementTest, CandidatesAreNonOverlappingAndRetainConfiguredSizes)
{
  const FillShapesConfig config{
      {{2000, 2000}, {1000, 1000}, {500, 500}}, 300, 500, 0};
  Polygon90Set free;
  free.insert(Rectangle(0, 0, 12000, 9000));
  free -= makeRect(4000, 2000, 6000, 7000);
  const auto candidates = makeFillCandidates(free, layer_, config);
  ASSERT_FALSE(candidates.empty());
  Polygon90Set combined;
  double sum = 0;
  for (const auto& candidate : candidates) {
    const int width = xh(candidate) - xl(candidate);
    const int height = yh(candidate) - yl(candidate);
    EXPECT_EQ(width, height);
    EXPECT_TRUE(width == 2000 || width == 1000 || width == 500);
    combined.insert(candidate);
    sum += rectangleArea(candidate);
  }
  EXPECT_DOUBLE_EQ(boost::polygon::area(combined), sum);
  Polygon90Set outside = combined - free;
  EXPECT_DOUBLE_EQ(boost::polygon::area(outside), 0.0);
}

TEST_P(FillPlacementTest, RejectsUnsupportedOpcWithoutChangingExistingFill)
{
  auto* original
      = odb::dbFill::create(block_, false, 0, layer_, 5000, 5000, 7000, 7000);
  writeRules(true);
  LPFill filler(getDb(), getLogger());
  EXPECT_THROW(filler.fill(options()), std::runtime_error);
  ASSERT_EQ(block_->getFills().size(), 1);
  EXPECT_EQ(*block_->getFills().begin(), original);
}

TEST_P(FillPlacementTest, RollsBackEarlierLayersOnLaterFailure)
{
  tst::addRoutingLayer(tech_, "M3");
  {
    std::ofstream output(rules_);
    output << R"({"layers":{
      "M2":{"name":"M2","space_to_outline":0,"non-opc":{"datatype":0,
        "width":[1],"height":[1],"space_to_fill":0.3,"space_to_non_fill":0.5}},
      "M3":{"name":"M3","space_to_outline":0,"non-opc":{"datatype":0,
        "width":[1],"height":[1],"space_to_fill":0.3,"space_to_non_fill":0.5},
        "opc":{"halo":1,"width":[1],"height":[1],
        "space_to_fill":0.3,"space_to_non_fill":0.5}}
    }})";
  }
  LPFill filler(getDb(), getLogger());
  EXPECT_THROW(filler.fill(options()), std::runtime_error);
  EXPECT_TRUE(block_->getFills().empty());
}

TEST_P(FillPlacementTest, StaggerRulesUseMicronsAndDefaultToOff)
{
  auto layers = loadFillRules(rules_.string().c_str(), tech_, getLogger());
  EXPECT_FALSE(layers.at(layer_).non_opc.stagger);
  setStaggerRules(R"("stagger":"ON","stagger_offset":[1.25,0])");
  layers = loadFillRules(rules_.string().c_str(), tech_, getLogger());
  EXPECT_TRUE(layers.at(layer_).non_opc.stagger);
  EXPECT_EQ(layers.at(layer_).non_opc.stagger_offset, odb::Point(1250, 0));
  setStaggerRules(R"("stagger":"ON","stagger_offset":[0,-0.5])");
  layers = loadFillRules(rules_.string().c_str(), tech_, getLogger());
  EXPECT_EQ(layers.at(layer_).non_opc.stagger_offset, odb::Point(0, -500));
  setStaggerRules(R"("stagger":"OFF","stagger_offset":[0,-0.5])");
  layers = loadFillRules(rules_.string().c_str(), tech_, getLogger());
  EXPECT_FALSE(layers.at(layer_).non_opc.stagger);
}

TEST_P(FillPlacementTest, RejectsInvalidStaggerRulesBeforeInsertingFill)
{
  for (const std::string& fields :
       {R"("stagger":"ON")",
        R"("stagger":"UNKNOWN","stagger_offset":[0,0])",
        R"("stagger":["ON"],"stagger_offset":[0,0])",
        R"("stagger":"ON","stagger_offset":[1])",
        R"("stagger":"ON","stagger_offset":[1,2,3])",
        R"("stagger":"ON","stagger_offset":{"x":1,"y":0})",
        R"("stagger":"ON","stagger_offset":[1,1])",
        R"("stagger":"ON","stagger_offset":[1,-1])",
        R"("stagger":"OFF","stagger_offset":[1,1])",
        R"("stagger":"ON","stagger_offset":[0.00001,1])",
        R"("stagger":"ON","stagger_offset":[0,1e100])"}) {
    setStaggerRules(fields);
    LPFill filler(getDb(), getLogger());
    EXPECT_THROW(filler.fill(options()), std::exception) << fields;
    EXPECT_TRUE(block_->getFills().empty());
  }
}

TEST_P(FillPlacementTest, SingleAxisStaggerProducesLegalTileContainedFill)
{
  for (const std::string& fields :
       {R"("stagger":"ON","stagger_offset":[0.5,0])",
        R"("stagger":"ON","stagger_offset":[0,0.4])"}) {
    setStaggerRules(fields);
    LPFill filler(getDb(), getLogger());
    ASSERT_NO_THROW(filler.fill(options()));
    ASSERT_FALSE(block_->getFills().empty());
    std::vector<odb::Rect> fills;
    Polygon90Set area;
    double sum = 0.0;
    for (auto* fill : block_->getFills()) {
      odb::Rect rect;
      fill->getRect(rect);
      for (const auto& other : fills) {
        EXPECT_TRUE(int64_t{rect.xMax()} + 300 <= other.xMin()
                    || int64_t{other.xMax()} + 300 <= rect.xMin()
                    || int64_t{rect.yMax()} + 300 <= other.yMin()
                    || int64_t{other.yMax()} + 300 <= rect.yMin());
      }
      fills.push_back(rect);
      area.insert(
          Rectangle(rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax()));
      sum += rect.area();
    }
    EXPECT_DOUBLE_EQ(boost::polygon::area(area), sum);
    const auto all_fills = block_->getFills();
    const std::vector<odb::dbFill*> placed_fills(all_fills.begin(),
                                                 all_fills.end());
    for (auto* fill : placed_fills) {
      odb::dbFill::destroy(fill);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(BothObjectives,
                         FillPlacementTest,
                         testing::Values("min_fill", "min_var"));
}  // namespace
}  // namespace fin

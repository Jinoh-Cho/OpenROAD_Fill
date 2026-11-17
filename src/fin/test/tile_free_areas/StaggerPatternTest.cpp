// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "CandidateReference.h"
#include "CandidateSelection.h"
#include "DensityFill.h"
#include "FillConfig.h"
#include "FillPattern.h"
#include "LPFill.h"
#include "LPFillUtil.h"
#include "TileGrid.h"
#include "gtest/gtest.h"

namespace fin {
namespace {
class StaggerPatternTest : public ::testing::Test
{
 protected:
  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)> db{
      odb::dbDatabase::create(),
      &odb::dbDatabase::destroy};
  odb::dbTech* tech = odb::dbTech::create(db.get(), "tech");
  odb::dbTechLayer* layer
      = odb::dbTechLayer::create(tech, "M2", odb::dbTechLayerType::ROUTING);

  std::filesystem::path writeRules(const std::string& extra)
  {
    db->setDbuPerMicron(1000);
    const char* tmp = std::getenv("TEST_TMPDIR");
    const auto path
        = std::filesystem::path(tmp ? tmp : "/tmp") / "fin_stagger_rules.json";
    {
      std::ofstream out(path);
      out << R"({"layers":{"M2":{"name":"M2","space_to_outline":0,"non-opc":{"datatype":0,"width":[0.16],"height":[0.08],"space_to_fill":0.018,"space_to_non_fill":0.018)"
          << extra << "}}}}";
    }
    return path;
  }

  FillShapesConfig load(const std::string& extra)
  {
    const auto path = writeRules(extra);
    try {
      auto result
          = loadFillLayerConfigs(path.c_str(), tech, nullptr).at(layer).non_opc;
      std::filesystem::remove(path);
      return result;
    } catch (...) {
      std::filesystem::remove(path);
      throw;
    }
  }
};

TEST_F(StaggerPatternTest, OrdinaryDensityFillPlacesStaggerWithoutLp)
{
  const auto rules = writeRules(R"(,"pattern":"stagger","stagger_offset":0.5)");
  auto* chip = odb::dbChip::create(db.get(), tech);
  auto* block = odb::dbBlock::create(chip, "pattern_density");
  const odb::Rect region(-100, -100, 1900, 1900);
  block->setDieArea(region);
  layer->setDirection(odb::dbTechLayerDir::HORIZONTAL);
  utl::Logger logger;
  DensityFill filler(db.get(), &logger, false);
  filler.fill(rules.c_str(), region);
  ASSERT_FALSE(block->getFills().empty());
  for (auto* fill : block->getFills()) {
    odb::Rect rect;
    fill->getRect(rect);
    const int row = (rect.yMin() - region.yMin()) / 98;
    EXPECT_EQ((rect.yMin() - region.yMin()) % 98, 0);
    EXPECT_EQ((rect.xMin() - region.xMin() - (row % 2 ? 89 : 0)) % 178, 0);
  }
  std::filesystem::remove(rules);
}

TEST_F(StaggerPatternTest, FullLpFlowSupportsBothPoliciesAndOrigin)
{
  const auto rules = writeRules(R"(,"pattern":"stagger","stagger_offset":0.5)");
  auto* chip = odb::dbChip::create(db.get(), tech);
  auto* block = odb::dbBlock::create(chip, "pattern_lp");
  const odb::Rect region(-100, -100, 1900, 1900);
  block->setDieArea(region);
  layer->setDirection(odb::dbTechLayerDir::HORIZONTAL);
  const odb::Point origin(-70, -60);
  utl::Logger logger;
  LPFill filler(db.get(), &logger);
  for (const char* policy : {"greedy", "uniform"}) {
    const double placed = filler.fillLp(rules.c_str(),
                                        region,
                                        origin,
                                        1000,
                                        2,
                                        0.0,
                                        1.0,
                                        .1,
                                        .6,
                                        "",
                                        "",
                                        FillMethod::MinFillAmount,
                                        "pdlp",
                                        1,
                                        "",
                                        4,
                                        4,
                                        true,
                                        policy);
    EXPECT_GT(placed, 0);
    for (auto* fill : block->getFills()) {
      odb::Rect rect;
      fill->getRect(rect);
      const int row = (rect.yMin() - origin.y()) / 98;
      EXPECT_EQ((rect.yMin() - origin.y()) % 98, 0);
      EXPECT_EQ((rect.xMin() - origin.x() - (row % 2 ? 89 : 0)) % 178, 0);
    }
    std::vector<odb::dbFill*> remove;
    for (auto* fill : block->getFills()) {
      remove.push_back(fill);
    }
    for (auto* fill : remove) {
      odb::dbFill::destroy(fill);
    }
  }
  std::filesystem::remove(rules);
}

TEST_F(StaggerPatternTest, JsonDefaultAndExplicitFraction)
{
  const auto grid = load("");
  EXPECT_EQ(grid.pattern.type, FillPattern::Grid);
  EXPECT_EQ(grid.shapes[0], std::make_pair(160, 80));
  const auto stagger = load(R"(,"pattern":"stagger","stagger_offset":0.5)");
  EXPECT_EQ(stagger.pattern.type, FillPattern::Stagger);
  EXPECT_EQ(stagger.pattern.stagger_offset, .5);
  for (const auto extra : {R"(,"pattern":"stagger")",
                           R"(,"pattern":"stagger","stagger_offset":-0.1)",
                           R"(,"pattern":"stagger","stagger_offset":1)",
                           R"(,"pattern":"stagger","stagger_offset":"bad")",
                           R"(,"pattern":"grid","stagger_offset":0.5)",
                           R"(,"pattern":"unknown")"}) {
    EXPECT_THROW(load(extra), std::invalid_argument) << extra;
  }
}

TEST_F(StaggerPatternTest, ExactOffsetsAndGlobalPhaseAcrossSeparatedBounds)
{
  for (double offset : {0.0, .25, .5, .75}) {
    FillPatternConfig pattern{FillPattern::Stagger, offset, odb::Point(-3, -5)};
    Polygon90Set all, left, right;
    insertPatternRectangles(
        all, Rectangle(-10, -10, 50, 40), 4, 4, 4, 4, pattern);
    insertPatternRectangles(
        left, Rectangle(-10, -10, 22, 40), 4, 4, 4, 4, pattern);
    insertPatternRectangles(
        right, Rectangle(22, -10, 50, 40), 4, 4, 4, 4, pattern);
    std::vector<Rectangle> rectangles;
    all.get_rectangles(rectangles);
    ASSERT_FALSE(rectangles.empty());
    for (const auto& rect : rectangles) {
      const int row = (yl(rect) + 5) / 8;
      const int shift = row % 2 != 0 ? std::lround(offset * 8) : 0;
      EXPECT_EQ((xl(rect) + 3 - shift) % 8, 0);
      EXPECT_EQ((yl(rect) + 5) % 8, 0);
      EXPECT_EQ(unshiftPatternX(xl(rect), yl(rect), 8, 8, pattern),
                xl(rect) - shift);
    }
    // Compare each bounded call to the full lattice, excluding crossing fills.
    for (auto* subset : {&left, &right}) {
      std::vector<Rectangle> items;
      subset->get_rectangles(items);
      for (const auto& rect : items) {
        EXPECT_NE(std::find(rectangles.begin(), rectangles.end(), rect),
                  rectangles.end());
      }
    }
  }
}

TEST_F(StaggerPatternTest, UniformUsesLogicalNeighborsAcrossAlternatingRows)
{
  for (const auto origin : {odb::Point(0, 0), odb::Point(-17, -19)}) {
    FillPatternConfig pattern{FillPattern::Stagger, .5, origin};
    std::vector<Rectangle> candidates;
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        const int x = origin.x() + col * 4 + (row % 2 ? 2 : 0);
        const int y = origin.y() + row * 4;
        candidates.emplace_back(x, y, x + 2, y + 2);
      }
    }
    lpfill::UniformSelectionStats stats;
    std::vector<Rectangle> selected;
    const double area
        = lpfill::selectUniformCandidates(candidates,
                                          4,
                                          2,
                                          2,
                                          selected,
                                          lpfill::UniformBackend::Auto,
                                          &stats,
                                          {},
                                          pattern);
    EXPECT_EQ(area, 4);
    ASSERT_EQ(selected.size(), 1);
    EXPECT_TRUE(selected[0] == candidates[4]);
    EXPECT_EQ(stats.bitmap_groups, 1);
  }
}

TEST_F(StaggerPatternTest, UniformPrioritiesMatchGridInLogicalCoordinates)
{
  const odb::Point origin(-17, -19);
  std::vector<Rectangle> grid;
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 9; ++col) {
      const int x = origin.x() + col * 4, y = origin.y() + row * 4;
      grid.emplace_back(x, y, x + 2, y + 2);
    }
  }
  for (double offset : {.25, .5, .75}) {
    const FillPatternConfig pattern{FillPattern::Stagger, offset, origin};
    const auto shiftRect = [&](const Rectangle& rect) {
      const int row = (yl(rect) - origin.y()) / 4;
      const int shift = row % 2 ? std::lround(offset * 4) : 0;
      return Rectangle(xl(rect) + shift, yl(rect), xh(rect) + shift, yh(rect));
    };
    std::vector<Rectangle> stagger;
    std::transform(
        grid.begin(), grid.end(), std::back_inserter(stagger), shiftRect);
    for (double target : {.002, 4.0, 17.1, 1000.0}) {
      std::vector<Rectangle> expected, actual, transformed;
      const auto a
          = lpfill::selectUniformCandidates(grid, target, 2, 2, expected);
      const auto b
          = lpfill::selectUniformCandidates(stagger,
                                            target,
                                            2,
                                            2,
                                            actual,
                                            lpfill::UniformBackend::Auto,
                                            nullptr,
                                            {},
                                            pattern);
      std::transform(expected.begin(),
                     expected.end(),
                     std::back_inserter(transformed),
                     shiftRect);
      EXPECT_EQ(a, b);
      EXPECT_TRUE(transformed == actual);
    }
  }
}

TEST_F(StaggerPatternTest, DefaultGridRetainsZeroSpacingBoundaryBehavior)
{
  layer->setDirection(odb::dbTechLayerDir::HORIZONTAL);
  for (int spacing : {0, 1, 2}) {
    FillShapesConfig config{{{6, 4}, {2, 2}}, spacing, 1, 0};
    for (const auto& bounds :
         {Rectangle(0, 0, 8, 7), Rectangle(-9, -7, 32, 18)}) {
      Polygon90Set area;
      area.insert(bounds);
      std::vector<Polygon90> polygons;
      area.get(polygons);
      for (const auto& polygon : polygons) {
        const auto before = lpfill::test_reference::makeFillCandidatesBefore(
            polygon, layer, config, nullptr);
        const auto after
            = lpfill::makeFillCandidates(polygon, layer, config, nullptr);
        EXPECT_TRUE(before == after);
      }
    }
  }
}

TEST_F(StaggerPatternTest, TileSpacingAndDensityMatchForBothSelections)
{
  for (const auto direction :
       {odb::dbTechLayerDir::HORIZONTAL, odb::dbTechLayerDir::VERTICAL}) {
    layer->setDirection(direction);
    for (double offset : {.25, .5, .75}) {
      FillShapesConfig config{{{6, 4}, {2, 2}}, 2, 2, 4};
      config.pattern = {FillPattern::Stagger, offset, odb::Point(-3, 7)};
      TileGrid grid({odb::Rect(-12, -8, 83, 62), config.pattern.origin, 40, 2});
      const std::vector<Rectangle> metal{{-1, -40, 5, 100}, {-30, 24, 100, 29}};
      const std::vector<Rectangle> expanded{{-3, -42, 7, 102},
                                            {-32, 22, 102, 31}};
      grid.calculateMetalDensitiesFromRectangles(metal, 4);
      const auto areas = lpfill::makeLayerFillAreasFromRectangles(
          grid, expanded, layer, config, nullptr, 4);
      const auto generated = lpfill::makeLayerFillCandidates(
          areas.tile_areas, layer, config, nullptr, 4);
      const auto [sx, sy] = lpfill::getSpacing(layer, config);
      std::vector<Rectangle> all;
      std::vector<double> targets;
      for (size_t i = 0; i < generated.tiles.size(); ++i) {
        double capacity = 0;
        const auto& tile = grid.tiles()[i];
        for (const auto& rect : generated.tiles[i]) {
          EXPECT_GE(xl(rect), tile.xMin());
          EXPECT_LE(xh(rect), tile.xMax());
          EXPECT_GE(yl(rect), tile.yMin());
          EXPECT_LE(yh(rect), tile.yMax());
          capacity += boost::polygon::area(rect);
          all.push_back(rect);
        }
        targets.push_back(capacity * .2317);
      }
      ASSERT_FALSE(all.empty());
      for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
          const auto& a = all[i];
          const auto& b = all[j];
          EXPECT_TRUE(xh(a) + sx <= xl(b) || xh(b) + sx <= xl(a)
                      || yh(a) + sy <= yl(b) || yh(b) + sy <= yl(a));
        }
      }
      for (auto policy : {lpfill::SelectionPolicy::Greedy,
                          lpfill::SelectionPolicy::Uniform}) {
        auto serial = lpfill::selectLayerFillCandidates(
            generated.tiles, targets, 1, policy, sx, sy, config.pattern);
        auto parallel = lpfill::selectLayerFillCandidates(
            generated.tiles, targets, 4, policy, sx, sy, config.pattern);
        EXPECT_TRUE(serial.fills == parallel.fills);
        auto incremental = grid;
        incremental.addDisjointFillAreas(parallel.placed_areas);
        Polygon90Set combined;
        combined.insert(metal.begin(), metal.end());
        combined += parallel.selected_shapes;
        auto reference = grid;
        reference.calculateMetalDensities(combined);
        EXPECT_EQ(incremental.metalAreas(), reference.metalAreas());
        for (size_t i = 0; i < reference.windows().size(); ++i) {
          EXPECT_DOUBLE_EQ(incremental.windows()[i].metal_area,
                           reference.windows()[i].metal_area);
        }
      }
    }
  }
}
}  // namespace
}  // namespace fin

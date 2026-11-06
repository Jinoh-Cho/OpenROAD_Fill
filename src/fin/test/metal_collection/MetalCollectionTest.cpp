// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

#include "FillGeometry.h"
#include "gtest/gtest.h"
#include "odb/dbShape.h"
#include "odb/dbTransform.h"
#include "odb/dbWireCodec.h"
namespace fin {
namespace {
// Optional integration check using an existing small routed ODB, never
// modified.
TEST(MetalCollectionOdbTest, SerialAndParallelReadExistingLayout)
{
  const char* path = std::getenv("FIN_METAL_TEST_ODB");
  if (path == nullptr) {
    GTEST_SKIP() << "Set FIN_METAL_TEST_ODB to check a routed ODB.";
  }
  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)>
      database(odb::dbDatabase::create(), &odb::dbDatabase::destroy);
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input.good());
  database->read(input);
  auto* block = database->getChip()->getBlock();
  auto* layer = block->getTech()->findLayer("M2");
  ASSERT_NE(layer, nullptr);
  Polygon90Set serial;
  for (int threads : {1, 16}) {
    const auto start = std::chrono::steady_clock::now();
    std::map<std::string, double> phases;
    int actual = 0;
    auto shapes = orNonFills(block, layer, &phases, threads, &actual);
    const double elapsed = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - start)
                               .count();
    std::cout << "METAL_COLLECTION_CHECK: requested=" << threads
              << " actual=" << actual << " total_seconds=" << elapsed
              << std::endl;
    for (const auto& [name, value] : phases) {
      std::cout << name << "=" << value << std::endl;
    }
    if (threads == 1) {
      serial = shapes;
    } else {
      ASSERT_TRUE(std::equal(
          serial.begin(), serial.end(), shapes.begin(), shapes.end()));
      Polygon90Set missing = serial - shapes;
      Polygon90Set extra = shapes - serial;
      EXPECT_EQ(boost::polygon::area(missing), 0);
      EXPECT_EQ(boost::polygon::area(extra), 0);
    }
  }
}

class MetalCollectionTest : public testing::Test
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
TEST_F(MetalCollectionTest, ParallelMetalCollectionMatchesGeometryAcrossBatches)
{
  auto* tech = layer_->getTech();
  layer_->setWidth(4);
  auto* chip = odb::dbChip::create(db_, tech);
  auto* block = odb::dbBlock::create(chip, "top");
  auto* other
      = odb::dbTechLayer::create(tech, "M3", odb::dbTechLayerType::ROUTING);
  other->setWidth(4);
  auto* lib = odb::dbLib::create(db_, "lib", tech, ',');
  auto* master = odb::dbMaster::create(lib, "cell");
  master->setWidth(20);
  master->setHeight(20);
  master->setType(odb::dbMasterType::CORE);
  odb::dbBox::create(master, layer_, 0, 0, 4, 8);
  auto* term = odb::dbMTerm::create(
      master, "A", odb::dbIoType::INPUT, odb::dbSigType::SIGNAL);
  auto* pin = odb::dbMPin::create(term);
  odb::dbBox::create(pin, layer_, 10, 0, 14, 4);
  master->setFrozen();
  auto* via = odb::dbVia::create(block, "via");
  odb::dbBox::create(via, layer_, -2, -2, 2, 2);
  odb::dbBox::create(via, other, -4, -4, 4, 4);
  Polygon90Set expected;
  // Include empty nets and geometry after the 65536-object batch boundary.
  for (int index = 0; index < 65540; ++index) {
    auto* net
        = odb::dbNet::create(block, ("n" + std::to_string(index)).c_str());
    if (index >= 128 && index < 65536) {
      continue;
    }
    const int x = index * 40 - 1000;
    odb::dbWireEncoder encoder;
    encoder.begin(odb::dbWire::create(net));
    encoder.newPath(layer_, odb::dbWireType::ROUTED);
    encoder.addPoint(x, 0);
    encoder.addPoint(x + 20, 0);
    encoder.addVia(via);
    encoder.end();
    expected.insert(Rectangle(x - 2, -2, x + 22, 2));
    expected.insert(Rectangle(x + 18, -2, x + 22, 2));
    auto* swire = odb::dbSWire::create(net, odb::dbWireType::ROUTED);
    ASSERT_NE(odb::dbSBox::create(swire,
                                  layer_,
                                  x,
                                  20,
                                  x + 20,
                                  24,
                                  odb::dbWireShapeType::STRIPE,
                                  odb::dbSBox::HORIZONTAL),
              nullptr);
    ASSERT_NE(
        odb::dbSBox::create(swire, via, x, 40, odb::dbWireShapeType::STRIPE),
        nullptr);
    expected.insert(Rectangle(x, 20, x + 20, 24));
    expected.insert(Rectangle(x - 2, 38, x + 2, 42));
    auto* inst = odb::dbInst::create(
        block, master, ("i" + std::to_string(index)).c_str());
    inst->setOrient(index % 2 ? odb::dbOrientType::R90 : odb::dbOrientType::MX);
    inst->setOrigin(x, 60);
    for (auto box : {odb::Rect(0, 0, 4, 8), odb::Rect(10, 0, 14, 4)}) {
      inst->getTransform().apply(box);
      expected.insert(
          Rectangle(box.xMin(), box.yMin(), box.xMax(), box.yMax()));
    }
  }
  const auto serial = orNonFills(block, layer_);
  for (int threads : {1, 2, 16}) {
    std::map<std::string, double> phases;
    int actual = 0;
    Polygon90Set expanded;
    const auto parallel
        = orNonFills(block, layer_, &phases, threads, &actual, &expanded, 3);
    const Polygon90Set expected_expanded = serial + 3;
    Polygon90Set missing_expanded = expected_expanded - expanded;
    Polygon90Set extra_expanded = expanded - expected_expanded;
    EXPECT_EQ(boost::polygon::area(missing_expanded), 0);
    EXPECT_EQ(boost::polygon::area(extra_expanded), 0);
    std::vector<Rectangle> raw_expanded, raw_source;
    const auto raw_original = orNonFills(block,
                                         layer_,
                                         nullptr,
                                         threads,
                                         nullptr,
                                         nullptr,
                                         3,
                                         nullptr,
                                         &raw_expanded,
                                         &raw_source);
    Polygon90Set original_union;
    original_union.insert(raw_source.begin(), raw_source.end());
    // Boolean operations may normalize their const operands; preserve raw
    // order.
    Polygon90Set source_reference = serial;
    Polygon90Set original_missing = source_reference - original_union;
    Polygon90Set original_extra = original_union - source_reference;
    EXPECT_EQ(boost::polygon::area(original_missing), 0);
    EXPECT_EQ(boost::polygon::area(original_extra), 0);
    Polygon90Set raw_union;
    raw_union.insert(raw_expanded.begin(), raw_expanded.end());
    Polygon90Set raw_missing = expected_expanded - raw_union;
    Polygon90Set raw_extra = raw_union - expected_expanded;
    EXPECT_EQ(boost::polygon::area(raw_missing), 0);
    EXPECT_EQ(boost::polygon::area(raw_extra), 0);
    EXPECT_TRUE(std::equal(serial.begin(),
                           serial.end(),
                           raw_original.begin(),
                           raw_original.end()));
    // Compare raw events too: original ordering is preserved before union.
    EXPECT_TRUE(std::equal(
        serial.begin(), serial.end(), parallel.begin(), parallel.end()));
    EXPECT_GE(actual, 1);
    EXPECT_LE(actual, threads);
    Polygon90Set missing = expected - parallel;
    Polygon90Set extra = parallel - expected;
    EXPECT_EQ(boost::polygon::area(missing), 0);
    EXPECT_EQ(boost::polygon::area(extra), 0);
    for (const auto& [name, elapsed] : phases) {
      EXPECT_GE(elapsed, 0.0) << name;
    }
  }
  EXPECT_THROW(orNonFills(block, layer_, nullptr, 0), std::invalid_argument);
}

TEST_F(MetalCollectionTest, RectangleExpansionMatchesUnionWithHolesAndOverlaps)
{
  std::vector<odb::Rect> rectangles = {{0, 0, 100, 10},
                                       {0, 90, 100, 100},
                                       {0, 0, 10, 100},
                                       {90, 0, 100, 100},
                                       {-40, -30, -20, -10},
                                       {-20, -30, 0, -10},
                                       {110, 0, 120, 10},
                                       {30, 30, 35, 35}};
  std::mt19937 random(42);
  for (int i = 0; i < 100; ++i) {
    const int x = static_cast<int>(random() % 100) - 50;
    const int y = static_cast<int>(random() % 100) - 50;
    rectangles.emplace_back(x, y, x + 1 + random() % 20, y + 1 + random() % 20);
  }
  for (int spacing : {0, 1, 3, 10, 50}) {
    Polygon90Set original, expanded;
    for (const auto& rect : rectangles) {
      odb::dbShape shape;
      shape.setSegment(layer_, rect);
      insertShape(shape, original, layer_, &expanded, spacing);
    }
    const Polygon90Set reference = original + spacing;
    Polygon90Set missing = reference - expanded;
    Polygon90Set extra = expanded - reference;
    EXPECT_EQ(boost::polygon::area(missing), 0) << spacing;
    EXPECT_EQ(boost::polygon::area(extra), 0) << spacing;
  }
  Polygon90Set original, expanded;
  odb::dbShape shape;
  const int hi = std::numeric_limits<int>::max();
  shape.setSegment(layer_, odb::Rect(hi - 10, 0, hi, 10));
  EXPECT_THROW(insertShape(shape, original, layer_, &expanded, 1),
               std::overflow_error);
  auto* chip = odb::dbChip::create(db_, layer_->getTech());
  auto* block = odb::dbBlock::create(chip, "negative_spacing");
  EXPECT_THROW(orNonFills(block, layer_, nullptr, 1, nullptr, &expanded, -1),
               std::invalid_argument);
}

TEST_F(MetalCollectionTest, DegenerateInstanceUsesLegacyExpansion)
{
  auto* tech = layer_->getTech();
  auto* block
      = odb::dbBlock::create(odb::dbChip::create(db_, tech), "degenerate");
  auto* lib = odb::dbLib::create(db_, "lib", tech, ',');
  auto* master = odb::dbMaster::create(lib, "cell");
  master->setWidth(20);
  master->setHeight(20);
  odb::dbBox::create(master, layer_, 0, 0, 0, 10);
  odb::dbBox::create(master, layer_, 4, 4, 12, 12);
  master->setFrozen();
  ASSERT_NE(odb::dbInst::create(block, master, "inst"), nullptr);
  const auto original = orNonFills(block, layer_);
  const Polygon90Set reference = original + 3;
  for (int threads : {1, 16}) {
    Polygon90Set expanded;
    std::vector<Rectangle> raw_expanded, raw_source;
    bool fused = true;
    const auto actual = orNonFills(block,
                                   layer_,
                                   nullptr,
                                   threads,
                                   nullptr,
                                   nullptr,
                                   3,
                                   &fused,
                                   &raw_expanded,
                                   &raw_source);
    expanded.insert(raw_expanded.begin(), raw_expanded.end());
    Polygon90Set source_union;
    source_union.insert(raw_source.begin(), raw_source.end());
    Polygon90Set source_reference = original;
    Polygon90Set source_missing = source_reference - source_union;
    Polygon90Set source_extra = source_union - source_reference;
    EXPECT_EQ(boost::polygon::area(source_missing), 0);
    EXPECT_EQ(boost::polygon::area(source_extra), 0);
    EXPECT_FALSE(fused);
    EXPECT_TRUE(std::equal(
        original.begin(), original.end(), actual.begin(), actual.end()));
    Polygon90Set missing = reference - expanded;
    Polygon90Set extra = expanded - reference;
    EXPECT_EQ(boost::polygon::area(missing), 0);
    EXPECT_EQ(boost::polygon::area(extra), 0);
  }
}

TEST(MetalCollectionOdbTest, FusedExpansionMatchesLegacyOnExistingLayout)
{
  const char* path = std::getenv("FIN_METAL_TEST_ODB");
  if (path == nullptr) {
    GTEST_SKIP() << "Set FIN_METAL_TEST_ODB to check a routed ODB.";
  }
  std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)>
      database(odb::dbDatabase::create(), &odb::dbDatabase::destroy);
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input.good());
  database->read(input);
  auto* block = database->getChip()->getBlock();
  auto* layer = block->getTech()->findLayer("M2");
  ASSERT_NE(layer, nullptr);
  const int spacing = static_cast<int>(0.018 * block->getDbUnitsPerMicron());
  const auto die = block->getDieArea();
  Polygon90Set region;
  region.insert(Rectangle(die.xMin(), die.yMin(), die.xMax(), die.yMax()));
  using Clock = std::chrono::steady_clock;
  const auto legacy_start = Clock::now();
  const auto original = orNonFills(block, layer, nullptr, 16);
  const Polygon90Set legacy = original + spacing;
  const double legacy_prepare
      = std::chrono::duration<double>(Clock::now() - legacy_start).count();
  Polygon90Set legacy_free = region - legacy;
  std::vector<Rectangle> expected_free;
  legacy_free.get_rectangles(expected_free);
  const double legacy_total
      = std::chrono::duration<double>(Clock::now() - legacy_start).count();
  for (int threads : {1, 16}) {
    const auto fused_start = Clock::now();
    Polygon90Set expanded;
    bool fused_spacing = false;
    const auto fused_original = orNonFills(block,
                                           layer,
                                           nullptr,
                                           threads,
                                           nullptr,
                                           &expanded,
                                           spacing,
                                           &fused_spacing);
    const double fused_prepare
        = std::chrono::duration<double>(Clock::now() - fused_start).count();
    Polygon90Set free = region - expanded;
    std::vector<Rectangle> actual_free;
    free.get_rectangles(actual_free);
    const double fused_total
        = std::chrono::duration<double>(Clock::now() - fused_start).count();
    EXPECT_EQ(actual_free, expected_free);
    EXPECT_TRUE(std::equal(original.begin(),
                           original.end(),
                           fused_original.begin(),
                           fused_original.end()));
    Polygon90Set missing = legacy - expanded;
    Polygon90Set extra = expanded - legacy;
    EXPECT_EQ(boost::polygon::area(missing), 0);
    EXPECT_EQ(boost::polygon::area(extra), 0);
    std::cout << "FUSED_SPACING_CHECK: spacing=" << spacing
              << " threads=" << threads << " spacing_fused=" << fused_spacing
              << " legacy_prepare_seconds=" << legacy_prepare
              << " legacy_through_free_decompose_seconds=" << legacy_total
              << " fused_prepare_seconds=" << fused_prepare
              << " fused_through_free_decompose_seconds=" << fused_total
              << std::endl;
  }
}

TEST_F(MetalCollectionTest, ParallelMetalCollectionHandlesEmptyBlock)
{
  auto* chip = odb::dbChip::create(db_, layer_->getTech());
  auto* block = odb::dbBlock::create(chip, "empty");
  int actual = 0;
  const auto shapes = orNonFills(block, layer_, nullptr, 16, &actual);
  EXPECT_EQ(boost::polygon::area(shapes), 0);
  EXPECT_EQ(actual, 1);
}

}  // namespace
}  // namespace fin

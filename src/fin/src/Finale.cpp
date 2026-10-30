// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#include "fin/Finale.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "DensityFill.h"
#include "LPFill.h"
#include "LpDiagnostics.h"
#include "odb/db.h"
#include "odb/dbShape.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

////////////////////////////////////////////////////////////////

Finale::Finale(odb::dbDatabase* db, utl::Logger* logger)
    : db_(db), logger_(logger)
{
}

void Finale::densityFill(const char* rules_filename, const odb::Rect& fill_area)
{
  DensityFill filler(db_, logger_, false);
  filler.fill(rules_filename, fill_area);
}

std::string Finale::layoutFeatureCounts()
{
  auto* chip = db_->getChip();
  if (chip == nullptr || chip->getBlock() == nullptr) {
    throw std::runtime_error("Load a design before counting layout features.");
  }
  auto* block = chip->getBlock();
  struct Counts
  {
    uint64_t fills = 0;
    uint64_t regular_wires = 0;
    uint64_t special_wires = 0;
  };
  std::unordered_map<odb::dbTechLayer*, Counts> layers;
  for (auto* layer : db_->getTech()->getLayers()) {
    if (layer->getType() == odb::dbTechLayerType::ROUTING) {
      layers[layer] = {};
    }
  }
  for (auto* fill : block->getFills()) {
    const auto it = layers.find(fill->getTechLayer());
    if (it != layers.end()) {
      ++it->second.fills;
    }
  }
  odb::dbWireShapeItr shapes;
  odb::dbShape shape;
  for (auto* net : block->getNets()) {
    if (auto* wire = net->getWire()) {
      for (shapes.begin(wire); shapes.next(shape);) {
        // Count routed metal segments, not vias or via enclosure boxes.
        if (shape.getType() == odb::dbShape::SEGMENT) {
          const auto it = layers.find(shape.getTechLayer());
          if (it != layers.end()) {
            ++it->second.regular_wires;
          }
        }
      }
    }
    for (auto* swire : net->getSWires()) {
      for (auto* box : swire->getWires()) {
        if (!box->isVia()) {
          const auto it = layers.find(box->getTechLayer());
          if (it != layers.end()) {
            ++it->second.special_wires;
          }
        }
      }
    }
  }
  const auto fields = [](const Counts& counts) {
    std::ostringstream text;
    text << "\"fill_count\": " << counts.fills
         << ", \"wire_count\": " << counts.regular_wires + counts.special_wires
         << ", \"regular_wire_count\": " << counts.regular_wires
         << ", \"special_wire_count\": " << counts.special_wires;
    return text.str();
  };
  std::ostringstream per_layer;
  Counts total;
  bool first = true;
  for (auto* layer : db_->getTech()->getLayers()) {
    const auto it = layers.find(layer);
    if (it == layers.end()) {
      continue;
    }
    const auto& counts = it->second;
    total.fills += counts.fills;
    total.regular_wires += counts.regular_wires;
    total.special_wires += counts.special_wires;
    std::cout << "[Layout features] layer=" << layer->getConstName()
              << " fill_count=" << counts.fills
              << " wire_count=" << counts.regular_wires + counts.special_wires
              << " regular_wire_count=" << counts.regular_wires
              << " special_wire_count=" << counts.special_wires << std::endl;
    per_layer << (first ? "" : ", ")
              << "{\"layer\": " << lpJsonString(layer->getConstName()) << ", "
              << fields(counts) << '}';
    first = false;
  }
  return "{" + fields(total) + ", \"feature_counts_by_layer\": ["
         + per_layer.str() + "]}";
}

double Finale::tileGridMetalArea(const char* rules_filename,
                                 const odb::Rect& region,
                                 const odb::Point& origin,
                                 int window_size,
                                 int resolution,
                                 double min_window_density,
                                 double max_density,
                                 const char* svg_filename,
                                 const char* density_report_filename,
                                 const char* floating_density_profile_filename,
                                 const char* floating_density_algorithm)
{
  LPFill filler(db_, logger_);
  return filler.tileGridMetalArea(rules_filename,
                                  region,
                                  origin,
                                  window_size,
                                  resolution,
                                  min_window_density,
                                  max_density,
                                  svg_filename,
                                  density_report_filename,
                                  floating_density_profile_filename,
                                  floating_density_algorithm);
}

double Finale::fixedDissectionLpMinVarFill(const char* rules_filename,
                                           const odb::Rect& region,
                                           const odb::Point& origin,
                                           int window_size,
                                           int resolution,
                                           double min_tile_density,
                                           double max_tile_density,
                                           double min_window_density,
                                           double max_window_density,
                                           const char* svg_filename,
                                           const char* density_report_filename,
                                           const char* lp_solver,
                                           int lp_threads,
                                           const char* lp_diagnostics,
                                           int candidate_threads,
                                           int placement_threads)
{
  LPFill filler(db_, logger_);
  return filler.fillLp(rules_filename,
                       region,
                       origin,
                       window_size,
                       resolution,
                       min_tile_density,
                       max_tile_density,
                       min_window_density,
                       max_window_density,
                       svg_filename,
                       density_report_filename,
                       FillMethod::MinVar,
                       lp_solver,
                       lp_threads,
                       lp_diagnostics,
                       candidate_threads,
                       placement_threads);
}

double Finale::fixedDissectionLpMinAmountFill(
    const char* rules_filename,
    const odb::Rect& region,
    const odb::Point& origin,
    int window_size,
    int resolution,
    double min_tile_density,
    double max_tile_density,
    double min_window_density,
    double max_window_density,
    const char* svg_filename,
    const char* density_report_filename,
    const char* lp_solver,
    int lp_threads,
    const char* lp_diagnostics,
    int candidate_threads,
    int placement_threads)
{
  LPFill filler(db_, logger_);
  return filler.fillLp(rules_filename,
                       region,
                       origin,
                       window_size,
                       resolution,
                       min_tile_density,
                       max_tile_density,
                       min_window_density,
                       max_window_density,
                       svg_filename,
                       density_report_filename,
                       FillMethod::MinFillAmount,
                       lp_solver,
                       lp_threads,
                       lp_diagnostics,
                       candidate_threads,
                       placement_threads);
}

double Finale::fixedDissectionLpLipFill(const char* rules_filename,
                                        const odb::Rect& region,
                                        const odb::Point& origin,
                                        int window_size,
                                        int resolution,
                                        double min_tile_density,
                                        double max_tile_density,
                                        double min_window_density,
                                        double max_window_density,
                                        int lip_type,
                                        const char* svg_filename,
                                        const char* density_report_filename,
                                        const char* lp_solver,
                                        int lp_threads,
                                        const char* lp_diagnostics,
                                        int candidate_threads,
                                        int placement_threads)
{
  if (lip_type < 1 || lip_type > 3) {
    throw std::invalid_argument("Lip LP type must be 1, 2, or 3.");
  }
  constexpr FillMethod methods[]{
      FillMethod::Lip1, FillMethod::Lip2, FillMethod::Lip3};
  LPFill filler(db_, logger_);
  return filler.fillLp(rules_filename,
                       region,
                       origin,
                       window_size,
                       resolution,
                       min_tile_density,
                       max_tile_density,
                       min_window_density,
                       max_window_density,
                       svg_filename,
                       density_report_filename,
                       methods[lip_type - 1],
                       lp_solver,
                       lp_threads,
                       lp_diagnostics,
                       candidate_threads,
                       placement_threads);
}

}  // namespace fin

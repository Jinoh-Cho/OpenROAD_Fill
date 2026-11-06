// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillGeometry.h"

#include <omp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "odb/dbShape.h"
#include "odb/dbTypes.h"

namespace fin {

Polygon90 makeRect(int x_lo, int y_lo, int x_hi, int y_hi)
{
  using Point = Polygon90::point_type;
  const std::array<Point, 4> points = {Point(x_lo, y_lo),
                                       Point(x_hi, y_lo),
                                       Point(x_hi, y_hi),
                                       Point(x_lo, y_hi)};
  Polygon90 polygon;
  polygon.set(points.begin(), points.end());
  return polygon;
}

namespace {

class DegenerateMetalRectangle : public std::runtime_error
{
 public:
  DegenerateMetalRectangle()
      : std::runtime_error("Degenerate source metal rectangle.")
  {
  }
};

void insertRectangle(Polygon90Set& shapes,
                     Polygon90Set* expanded,
                     int spacing,
                     int x_lo,
                     int y_lo,
                     int x_hi,
                     int y_hi,
                     std::vector<Rectangle>* expanded_rectangles,
                     std::vector<Rectangle>* original_rectangles)
{
  if (original_rectangles != nullptr) {
    if (x_lo >= x_hi || y_lo >= y_hi) {
      throw DegenerateMetalRectangle();
    }
    original_rectangles->emplace_back(x_lo, y_lo, x_hi, y_hi);
  }
  if (expanded != nullptr || expanded_rectangles != nullptr) {
    if (spacing < 0) {
      throw std::invalid_argument(
          "Source rectangle expansion requires nonnegative spacing.");
    }
    if (x_lo >= x_hi || y_lo >= y_hi) {
      throw DegenerateMetalRectangle();
    }
    const int64_t left = static_cast<int64_t>(x_lo) - spacing;
    const int64_t bottom = static_cast<int64_t>(y_lo) - spacing;
    const int64_t right = static_cast<int64_t>(x_hi) + spacing;
    const int64_t top = static_cast<int64_t>(y_hi) + spacing;
    if (left < std::numeric_limits<int>::min()
        || bottom < std::numeric_limits<int>::min()
        || right > std::numeric_limits<int>::max()
        || top > std::numeric_limits<int>::max()) {
      throw std::overflow_error(
          "Expanded metal rectangle exceeds coordinate range.");
    }
    const Rectangle rectangle(static_cast<int>(left),
                              static_cast<int>(bottom),
                              static_cast<int>(right),
                              static_cast<int>(top));
    if (expanded != nullptr) {
      expanded->insert(rectangle);
    }
    if (expanded_rectangles != nullptr) {
      expanded_rectangles->push_back(rectangle);
    }
  }
  shapes.insert(makeRect(x_lo, y_lo, x_hi, y_hi));
}

}  // namespace

void insertShape(const odb::dbShape& shape,
                 Polygon90Set& polygon_set,
                 odb::dbTechLayer* layer,
                 Polygon90Set* expanded,
                 int spacing,
                 std::vector<Rectangle>* expanded_rectangles,
                 std::vector<Rectangle>* original_rectangles)
{
  switch (shape.getType()) {
    case odb::dbShape::VIA:
    case odb::dbShape::TECH_VIA: {
      odb::dbTechLayer* top_layer;
      odb::dbTechLayer* bottom_layer;
      if (shape.getType() == odb::dbShape::VIA) {
        auto* via = shape.getVia();
        top_layer = via->getTopLayer();
        bottom_layer = via->getBottomLayer();
      } else {
        auto* via = shape.getTechVia();
        top_layer = via->getTopLayer();
        bottom_layer = via->getBottomLayer();
      }
      if (top_layer != layer && bottom_layer != layer) {
        return;
      }
      std::vector<odb::dbShape> boxes;
      odb::dbShape::getViaBoxes(shape, boxes);
      for (const auto& box : boxes) {
        if (box.getTechLayer() == layer) {
          insertRectangle(polygon_set,
                          expanded,
                          spacing,
                          box.xMin(),
                          box.yMin(),
                          box.xMax(),
                          box.yMax(),
                          expanded_rectangles,
                          original_rectangles);
        }
      }
      break;
    }
    case odb::dbShape::SEGMENT:
    case odb::dbShape::TECH_VIA_BOX:
    case odb::dbShape::VIA_BOX:
      if (shape.getTechLayer() == layer) {
        insertRectangle(polygon_set,
                        expanded,
                        spacing,
                        shape.xMin(),
                        shape.yMin(),
                        shape.xMax(),
                        shape.yMax(),
                        expanded_rectangles,
                        original_rectangles);
      }
      break;
  }
}

namespace {

void appendRectangles(std::vector<Rectangle>* output,
                      const std::vector<std::vector<Rectangle>>& inputs)
{
  if (output != nullptr) {
    for (const auto& input : inputs) {
      output->insert(output->end(), input.begin(), input.end());
    }
  }
}

// Keep pointer/geometry buffers bounded even for hundreds of millions of
// objects. OpenDB is read-only here; iterators and polygon buffers belong to
// each worker.
template <typename Objects, typename Collect>
void collectObjects(Objects objects,
                    int threads,
                    const Collect& collect,
                    Polygon90Set& result,
                    Polygon90Set* expanded,
                    std::vector<Rectangle>* expanded_rectangles,
                    std::vector<Rectangle>* original_rectangles,
                    double& enumerate_seconds,
                    double& merge_seconds,
                    int& actual_threads)
{
  using Clock = std::chrono::steady_clock;
  if (threads == 1) {
    for (auto* object : objects) {
      collect(
          object, result, expanded, expanded_rectangles, original_rectangles);
    }
    return;
  }
  constexpr size_t batch_size = 65536;
  using Object = decltype(*objects.begin());
  std::vector<Object> batch;
  batch.reserve(batch_size);
  auto next = objects.begin();
  while (next != objects.end()) {
    const auto enumerate_start = Clock::now();
    batch.clear();
    for (; next != objects.end() && batch.size() < batch_size; ++next) {
      batch.push_back(*next);
    }
    enumerate_seconds
        += std::chrono::duration<double>(Clock::now() - enumerate_start)
               .count();
    const int requested = std::min<size_t>(threads, batch.size());
    std::vector<Polygon90Set> local(requested);
    std::vector<Polygon90Set> local_expanded(expanded ? requested : 0);
    std::vector<std::vector<Rectangle>> local_rectangles(
        expanded_rectangles ? requested : 0);
    std::vector<std::vector<Rectangle>> local_originals(
        original_rectangles ? requested : 0);
    std::exception_ptr error;
    std::mutex error_mutex;
#pragma omp parallel num_threads(requested)
    {
      const int thread = omp_get_thread_num();
#pragma omp single
      actual_threads = std::max(actual_threads, omp_get_num_threads());
      // Contiguous partitions and ordered concatenation retain original order.
#pragma omp for schedule(static)
      for (size_t index = 0; index < batch.size(); ++index) {
        try {
          collect(batch[index],
                  local[thread],
                  expanded ? &local_expanded[thread] : nullptr,
                  expanded_rectangles ? &local_rectangles[thread] : nullptr,
                  original_rectangles ? &local_originals[thread] : nullptr);
        } catch (...) {
          std::lock_guard<std::mutex> lock(error_mutex);
          if (!error) {
            error = std::current_exception();
          }
        }
      }
    }
    if (error) {
      std::rethrow_exception(error);
    }
    const auto merge_start = Clock::now();
    for (const auto& part : local) {
      // Append raw edge events, without triggering a polygon union per batch.
      result.insert(part);
    }
    if (expanded != nullptr) {
      for (const auto& part : local_expanded) {
        expanded->insert(part);
      }
    }
    appendRectangles(expanded_rectangles, local_rectangles);
    appendRectangles(original_rectangles, local_originals);
    // Include destruction of temporary edge buffers in the merge phase.
    local.clear();
    local_expanded.clear();
    local_rectangles.clear();
    local_originals.clear();
    merge_seconds
        += std::chrono::duration<double>(Clock::now() - merge_start).count();
  }
}

void collectSpecialNet(odb::dbNet* net,
                       odb::dbTechLayer* layer,
                       Polygon90Set& shapes,
                       Polygon90Set* expanded_shapes,
                       int spacing,
                       std::vector<Rectangle>* expanded_rectangles,
                       std::vector<Rectangle>* original_rectangles)
{
  odb::dbShape shape;
  std::vector<odb::dbShape> via_shapes;
  for (auto* swire : net->getSWires()) {
    for (auto* sbox : swire->getWires()) {
      if (sbox->isVia()) {
        shape.setVia(sbox->getBlockVia(), sbox->getBox());
        odb::dbShape::getViaBoxes(shape, via_shapes);
        for (const auto& via_shape : via_shapes) {
          insertShape(via_shape,
                      shapes,
                      layer,
                      expanded_shapes,
                      spacing,
                      expanded_rectangles,
                      original_rectangles);
        }
      } else if (sbox->getTechLayer() == layer) {
        insertRectangle(shapes,
                        expanded_shapes,
                        spacing,
                        sbox->xMin(),
                        sbox->yMin(),
                        sbox->xMax(),
                        sbox->yMax(),
                        expanded_rectangles,
                        original_rectangles);
      }
    }
  }
}

}  // namespace

namespace {

void collectRegularNet(odb::dbNet* net,
                       odb::dbTechLayer* layer,
                       Polygon90Set& shapes,
                       Polygon90Set* expanded_shapes,
                       int spacing,
                       std::vector<Rectangle>* rectangles,
                       std::vector<Rectangle>* originals)
{
  if (auto* wire = net->getWire()) {
    odb::dbShape shape;
    odb::dbWireShapeItr iterator;
    for (iterator.begin(wire); iterator.next(shape);) {
      insertShape(shape,
                  shapes,
                  layer,
                  expanded_shapes,
                  spacing,
                  rectangles,
                  originals);
    }
  }
}

void collectInstance(odb::dbInst* instance,
                     odb::dbTechLayer* layer,
                     Polygon90Set& shapes,
                     Polygon90Set* expanded_shapes,
                     int spacing,
                     std::vector<Rectangle>* rectangles,
                     std::vector<Rectangle>* originals)
{
  odb::dbShape shape;
  odb::dbInstShapeItr iterator(false);
  for (iterator.begin(instance, odb::dbInstShapeItr::ALL);
       iterator.next(shape);) {
    insertShape(
        shape, shapes, layer, expanded_shapes, spacing, rectangles, originals);
  }
}

void resetRectangleOutputs(Polygon90Set* expanded,
                           int spacing,
                           std::vector<Rectangle>* expanded_rectangles,
                           std::vector<Rectangle>* original_rectangles)
{
  if (expanded != nullptr || expanded_rectangles != nullptr) {
    if (spacing < 0) {
      throw std::invalid_argument(
          "Source rectangle expansion requires nonnegative spacing.");
    }
    if (expanded != nullptr) {
      expanded->clear();
    }
    if (expanded_rectangles != nullptr) {
      expanded_rectangles->clear();
    }
  }
  if (original_rectangles != nullptr) {
    original_rectangles->clear();
  }
}

Polygon90Set collectNonFillRectangles(
    odb::dbBlock* block,
    odb::dbTechLayer* layer,
    std::map<std::string, double>* phase_seconds,
    int threads,
    int* actual_threads,
    Polygon90Set* expanded,
    int spacing,
    std::vector<Rectangle>* expanded_rectangles,
    std::vector<Rectangle>* original_rectangles)
{
  if (threads < 1) {
    throw std::invalid_argument(
        "Metal collection thread count must be positive.");
  }
  resetRectangleOutputs(
      expanded, spacing, expanded_rectangles, original_rectangles);
  using Clock = std::chrono::steady_clock;
  auto phase_start = Clock::now();
  const auto finish_phase = [&](const char* name) {
    const auto end = Clock::now();
    if (phase_seconds != nullptr) {
      (*phase_seconds)[name]
          = std::chrono::duration<double>(end - phase_start).count();
    }
    phase_start = end;
  };
  Polygon90Set non_fill;
  int used_threads = 1;
  double enumerate_seconds = 0.0;
  double merge_seconds = 0.0;
  const auto run = [&](auto objects, const auto& collect) {
    collectObjects(objects,
                   threads,
                   collect,
                   non_fill,
                   expanded,
                   expanded_rectangles,
                   original_rectangles,
                   enumerate_seconds,
                   merge_seconds,
                   used_threads);
  };
  run(block->getNets(),
      [&](odb::dbNet* net,
          Polygon90Set& shapes,
          Polygon90Set* expanded_shapes,
          std::vector<Rectangle>* rectangles,
          std::vector<Rectangle>* originals) {
        collectRegularNet(net,
                          layer,
                          shapes,
                          expanded_shapes,
                          spacing,
                          rectangles,
                          originals);
      });
  finish_phase("initial_collect_regular_seconds");
  run(block->getNets(),
      [&](odb::dbNet* net,
          Polygon90Set& shapes,
          Polygon90Set* expanded_shapes,
          std::vector<Rectangle>* rectangles,
          std::vector<Rectangle>* originals) {
        collectSpecialNet(net,
                          layer,
                          shapes,
                          expanded_shapes,
                          spacing,
                          rectangles,
                          originals);
      });
  finish_phase("initial_collect_special_seconds");
  run(block->getInsts(),
      [&](odb::dbInst* instance,
          Polygon90Set& shapes,
          Polygon90Set* expanded_shapes,
          std::vector<Rectangle>* rectangles,
          std::vector<Rectangle>* originals) {
        collectInstance(instance,
                        layer,
                        shapes,
                        expanded_shapes,
                        spacing,
                        rectangles,
                        originals);
      });
  finish_phase("initial_collect_instances_seconds");
  if (phase_seconds != nullptr) {
    // These two diagnostics overlap the three elapsed phases above.
    (*phase_seconds)["initial_collect_enumerate_seconds"] = enumerate_seconds;
    (*phase_seconds)["initial_collect_merge_seconds"] = merge_seconds;
  }
  if (actual_threads != nullptr) {
    *actual_threads = used_threads;
  }
  return non_fill;
}

}  // namespace

Polygon90Set orNonFills(odb::dbBlock* block,
                        odb::dbTechLayer* layer,
                        std::map<std::string, double>* phase_seconds,
                        int threads,
                        int* actual_threads,
                        Polygon90Set* expanded,
                        int spacing,
                        bool* spacing_fused,
                        std::vector<Rectangle>* expanded_rectangles,
                        std::vector<Rectangle>* original_rectangles)
{
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  if (spacing_fused != nullptr) {
    *spacing_fused = expanded != nullptr || expanded_rectangles != nullptr;
  }
  try {
    return collectNonFillRectangles(block,
                                    layer,
                                    phase_seconds,
                                    threads,
                                    actual_threads,
                                    expanded,
                                    spacing,
                                    expanded_rectangles,
                                    original_rectangles);
  } catch (const DegenerateMetalRectangle&) {
    // Preserve legacy handling of invalid zero-area polygons, rather than
    // silently changing geometry or aborting a previously accepted design.
    const double retry
        = std::chrono::duration<double>(Clock::now() - start).count();
    auto original = collectNonFillRectangles(block,
                                             layer,
                                             phase_seconds,
                                             threads,
                                             actual_threads,
                                             nullptr,
                                             0,
                                             nullptr,
                                             nullptr);
    const auto bloat_start = Clock::now();
    Polygon90Set legacy = original + spacing;
    if (expanded != nullptr) {
      *expanded = legacy;
    }
    if (expanded_rectangles != nullptr) {
      expanded_rectangles->clear();
      legacy.get_rectangles(*expanded_rectangles);
    }
    if (original_rectangles != nullptr) {
      original_rectangles->clear();
      Polygon90Set normalized = original;
      normalized.get_rectangles(*original_rectangles);
    }
    if (phase_seconds != nullptr) {
      (*phase_seconds)["initial_collect_expansion_retry_seconds"] = retry;
      (*phase_seconds)["initial_collect_expansion_fallback_seconds"]
          = std::chrono::duration<double>(Clock::now() - bloat_start).count();
    }
    if (spacing_fused != nullptr) {
      *spacing_fused = false;
    }
    return original;
  }
}

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "LPFillUtil.h"

#include <omp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>

#include "FillGeometry.h"
#include "TileGrid.h"
#include "graphics.h"

namespace fin::lpfill {
using odb::dbTechLayer;
using odb::dbTechLayerDir;
using odb::Rect;

bool candidateTraceEnabled()
{
  static const bool enabled = [] {
    const char* value = std::getenv("FIN_TRACE_CANDIDATES");
    return value != nullptr && std::string(value) == "1";
  }();
  return enabled;
}

void traceCandidate(const std::string& message)
{
  if (!candidateTraceEnabled()) {
    return;
  }
  const auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  std::cout << "[Fill candidate] time_ms=" << time_ms << " " << message
            << std::endl;
}

namespace {

// Each task owns its output slot. Exceptions are propagated after every worker
// has joined, so no task can outlive the caller's captured data.
template <typename Task>
int runTileTasks(size_t count, int threads, bool serial, const Task& task)
{
  if (threads < 1) {
    throw std::invalid_argument("Tile task thread count must be positive.");
  }
  if (count == 0) {
    return 1;
  }
  const int requested_threads = serial ? 1 : std::min<size_t>(threads, count);
  if (requested_threads == 1) {
    for (size_t index = 0; index < count; ++index) {
      task(index);
    }
    return 1;
  }
  int actual_threads = 1;
  std::exception_ptr error;
  std::mutex error_mutex;
  std::atomic<bool> failed{false};
#pragma omp parallel num_threads(requested_threads)
  {
#pragma omp single
    actual_threads = omp_get_num_threads();
#pragma omp for schedule(dynamic, 16)
    for (size_t index = 0; index < count; ++index) {
      if (failed.load(std::memory_order_relaxed)) {
        continue;
      }
      try {
        task(index);
      } catch (...) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!error) {
          error = std::current_exception();
        }
        failed.store(true, std::memory_order_relaxed);
      }
    }
  }
  if (error) {
    std::rethrow_exception(error);
  }
  return actual_threads;
}

LayerFillAreas partitionFreeRectangles(const TileGrid& grid,
                                       const std::vector<Rectangle>& rectangles)
{
  const auto& tiles = grid.tiles();
  LayerFillAreas result;
  result.tile_areas.resize(tiles.size());
  result.bloated_non_fill_areas.reserve(tiles.size());
  for (const Rect& tile : tiles) {
    result.bloated_non_fill_areas.push_back(static_cast<double>(tile.area()));
  }
  if (tiles.empty()) {
    return result;
  }

  // Grid edge arrays also handle clipped edge tiles and negative coordinates.
  const size_t columns = grid.tileColumns();
  std::vector<int> x_edges{tiles.front().xMin()};
  std::vector<int> y_edges{tiles.front().yMin()};
  for (size_t column = 0; column < columns; column++) {
    x_edges.push_back(tiles[column].xMax());
  }
  for (size_t first = 0; first < tiles.size(); first += columns) {
    y_edges.push_back(tiles[first].yMax());
  }

  for (const Rectangle& rectangle : rectangles) {
    const size_t first_column
        = std::upper_bound(x_edges.begin(), x_edges.end(), xl(rectangle))
          - x_edges.begin() - 1;
    const size_t last_column
        = std::lower_bound(x_edges.begin(), x_edges.end(), xh(rectangle))
          - x_edges.begin();
    const size_t first_row
        = std::upper_bound(y_edges.begin(), y_edges.end(), yl(rectangle))
          - y_edges.begin() - 1;
    const size_t last_row
        = std::lower_bound(y_edges.begin(), y_edges.end(), yh(rectangle))
          - y_edges.begin();
    for (size_t row = first_row; row < last_row; row++) {
      for (size_t column = first_column; column < last_column; column++) {
        const size_t index = row * columns + column;
        const Rect& tile = tiles[index];
        const int x_min = std::max(xl(rectangle), tile.xMin());
        const int y_min = std::max(yl(rectangle), tile.yMin());
        const int x_max = std::min(xh(rectangle), tile.xMax());
        const int y_max = std::min(yh(rectangle), tile.yMax());
        result.tile_areas[index].insert(Rectangle(x_min, y_min, x_max, y_max));
        // get_rectangles() partitions the free union into disjoint rectangles.
        result.bloated_non_fill_areas[index]
            -= static_cast<double>(x_max - x_min) * (y_max - y_min);
      }
    }
  }
  return result;
}

}  // namespace

LayerFillAreas makeLayerFillAreas(const TileGrid& grid,
                                  const Polygon90Set& bloated_non_fill,
                                  dbTechLayer* layer,
                                  const FillShapesConfig& config,
                                  Graphics* graphics,
                                  int threads)
{
  if (threads < 1) {
    throw std::invalid_argument(
        "Candidate preparation thread count must be positive.");
  }
  using Clock = std::chrono::steady_clock;
  std::map<std::string, double> phases;
  auto phase_start = Clock::now();
  const auto finish_phase = [&](const char* name) {
    const auto end = Clock::now();
    phases[name] = std::chrono::duration<double>(end - phase_start).count();
    phase_start = end;
  };
  traceCandidate("layer_free_area.subtract begin");
  const Rect& region = grid.region();
  Polygon90Set free_area;
  free_area.insert(
      Rectangle(region.xMin(), region.yMin(), region.xMax(), region.yMax()));
  free_area -= bloated_non_fill;
  finish_phase("candidate_free_subtract_seconds");
  traceCandidate(
      "layer_free_area.subtract end; layer_free_area.get_rectangles begin");
  std::vector<Rectangle> rectangles;
  free_area.get_rectangles(rectangles);
  finish_phase("candidate_free_decompose_seconds");
  traceCandidate(
      "layer_free_area.get_rectangles end; layer_free_area.partition begin");
  LayerFillAreas result = partitionFreeRectangles(grid, rectangles);
  finish_phase("candidate_tile_partition_seconds");
  traceCandidate("layer_free_area.partition end");

  const auto [space_x, space_y] = getSpacing(layer, config);
  const bool serial = graphics != nullptr || candidateTraceEnabled();
  const auto apply_spacing = [&](size_t index) {
    if (candidateTraceEnabled()) {
      traceCandidate("prepare_tile begin index=" + std::to_string(index));
    }
    const Rect& tile = grid.tiles()[index];
    Polygon90Set inset_tile;
    inset_tile.insert(
        Rectangle(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax()));
    shrink(inset_tile, space_x, space_x, space_y, space_y);
    result.tile_areas[index] = result.tile_areas[index] & inset_tile;
  };
  result.threads
      = runTileTasks(grid.tiles().size(), threads, serial, apply_spacing);
  finish_phase("candidate_tile_spacing_seconds");
  const auto prune_tile = [&](size_t index) {
    prune(result.tile_areas[index], layer, config, graphics);
    traceCandidate("prepare_tile end");
  };
  runTileTasks(grid.tiles().size(), threads, serial, prune_tile);
  finish_phase("candidate_tile_prune_seconds");
  result.phase_seconds = std::move(phases);
  return result;
}

std::pair<int, int> getSpacing(dbTechLayer* layer, const FillShapesConfig& cfg)
{
  const bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  int space_x = cfg.space_to_fill;
  int space_y = space_x;
  if (is_horiz) {
    space_x = std::max(space_x, cfg.space_line_end);
  } else {
    space_y = std::max(space_y, cfg.space_line_end);
  }
  return std::make_pair(space_x, space_y);
}

LayerFillCandidates makeLayerFillCandidates(
    const std::vector<Polygon90Set>& tile_areas,
    dbTechLayer* layer,
    const FillShapesConfig& config,
    Graphics* graphics,
    int threads)
{
  if (threads < 1) {
    throw std::invalid_argument("Candidate thread count must be positive.");
  }
  using Clock = std::chrono::steady_clock;
  LayerFillCandidates result;
  result.tiles.resize(tile_areas.size());
  if (tile_areas.empty()) {
    return result;
  }
  const bool trace = candidateTraceEnabled();
  std::vector<double> tile_seconds(tile_areas.size());
  const auto generate = [&](size_t index) {
    if (trace) {
      traceCandidate("tile generation begin index=" + std::to_string(index));
    }
    const auto start = Clock::now();
    result.tiles[index]
        = makeTileFillCandidates(tile_areas[index], layer, config, graphics);
    tile_seconds[index]
        = std::chrono::duration<double>(Clock::now() - start).count();
    if (trace) {
      traceCandidate("tile generation end index=" + std::to_string(index)
                     + " elapsed_seconds="
                     + std::to_string(tile_seconds[index]));
    }
  };
  const auto start = Clock::now();
  result.threads = runTileTasks(
      tile_areas.size(), threads, graphics != nullptr || trace, generate);
  result.wall_seconds
      = std::chrono::duration<double>(Clock::now() - start).count();
  result.work_seconds
      = std::accumulate(tile_seconds.begin(), tile_seconds.end(), 0.0);
  return result;
}

LayerFillPlacement selectLayerFillCandidates(
    const std::vector<std::vector<Rectangle>>& candidates,
    const std::vector<double>& target_areas,
    int threads)
{
  if (candidates.size() != target_areas.size()) {
    throw std::invalid_argument("Each candidate tile must have a target area.");
  }
  using Clock = std::chrono::steady_clock;
  const auto prepare_start = Clock::now();
  LayerFillPlacement result;
  result.placed_areas.resize(candidates.size(), 0.0);
  std::vector<std::vector<Rectangle>> selected(candidates.size());
  std::vector<Polygon90Set> tile_shapes(candidates.size());
  std::vector<double> tile_seconds(candidates.size());
  const bool trace = candidateTraceEnabled();
  const auto select = [&](size_t index) {
    const auto start = Clock::now();
    for (const Rectangle& candidate : candidates[index]) {
      const double candidate_area
          = static_cast<double>(xh(candidate) - xl(candidate))
            * (yh(candidate) - yl(candidate));
      if (result.placed_areas[index] + candidate_area <= target_areas[index]) {
        selected[index].push_back(candidate);
        result.placed_areas[index] += candidate_area;
      }
    }
    tile_seconds[index]
        = std::chrono::duration<double>(Clock::now() - start).count();
    if (trace) {
      traceCandidate("select_tile index=" + std::to_string(index) + " selected="
                     + std::to_string(selected[index].size()) + " placed_area="
                     + std::to_string(result.placed_areas[index]));
    }
  };
  const auto start = Clock::now();
  result.phase_seconds["placement_setup_seconds"]
      = std::chrono::duration<double>(start - prepare_start).count();
  result.threads = runTileTasks(candidates.size(), threads, trace, select);
  result.select_seconds
      = std::chrono::duration<double>(Clock::now() - start).count();
  result.select_work_seconds
      = std::accumulate(tile_seconds.begin(), tile_seconds.end(), 0.0);
  result.phase_seconds["placement_select_seconds"] = result.select_seconds;
  // Keep geometry construction in a separate phase so the selection timer
  // covers only choosing candidates against each tile's LP target area.
  const auto build_shapes = [&](size_t index) {
    const auto shape_start = Clock::now();
    for (const Rectangle& fill : selected[index]) {
      tile_shapes[index] += makeRect(xl(fill), yl(fill), xh(fill), yh(fill));
    }
    tile_seconds[index]
        += std::chrono::duration<double>(Clock::now() - shape_start).count();
  };
  const auto shape_start = Clock::now();
  runTileTasks(candidates.size(), threads, trace, build_shapes);
  result.phase_seconds["placement_shape_seconds"]
      = std::chrono::duration<double>(Clock::now() - shape_start).count();
  const auto collect_start = Clock::now();
  size_t selected_count = 0;
  for (const auto& tile : selected) {
    selected_count += tile.size();
  }
  result.fills.reserve(selected_count);
  // Flatten in tile/candidate order to preserve DB IDs and mask assignment.
  for (size_t index = 0; index < candidates.size(); ++index) {
    result.fills.insert(
        result.fills.end(), selected[index].begin(), selected[index].end());
  }
  result.phase_seconds["placement_collect_seconds"]
      = std::chrono::duration<double>(Clock::now() - collect_start).count();
  const auto merge_start = Clock::now();
  for (const auto& shapes : tile_shapes) {
    result.selected_shapes += shapes;
  }
  result.phase_seconds["placement_merge_seconds"]
      = std::chrono::duration<double>(Clock::now() - merge_start).count();
  result.work_seconds
      = std::accumulate(tile_seconds.begin(), tile_seconds.end(), 0.0);
  const auto cleanup_start = Clock::now();
  // Include destruction of potentially large per-tile buffers in the profile.
  std::vector<std::vector<Rectangle>>{}.swap(selected);
  std::vector<Polygon90Set>{}.swap(tile_shapes);
  std::vector<double>{}.swap(tile_seconds);
  result.phase_seconds["placement_cleanup_seconds"]
      = std::chrono::duration<double>(Clock::now() - cleanup_start).count();
  result.wall_seconds
      = std::chrono::duration<double>(Clock::now() - prepare_start).count();
  double accounted = 0.0;
  for (const auto& [name, value] : result.phase_seconds) {
    accounted += value;
  }
  result.phase_seconds["placement_prepare_other_seconds"]
      = std::max(0.0, result.wall_seconds - accounted);
  return result;
}

void prune(Polygon90Set& fill_area,
           dbTechLayer* layer,
           const FillShapesConfig& cfg,
           Graphics* graphics)
{
  static_cast<void>(graphics);
  const auto [space_x, space_y] = getSpacing(layer, cfg);
  Polygon90Set pruned(fill_area);
  traceCandidate("prune.grow_and begin");
  grow_and(pruned, space_x, space_x, space_y, space_y);
  traceCandidate("prune.grow_and end; prune.subtract begin");
  fill_area -= pruned;
  traceCandidate("prune.subtract end");
}

std::vector<Rectangle> makeFillCandidates(const Polygon90& area,
                                          dbTechLayer* layer,
                                          const FillShapesConfig& config,
                                          Graphics* graphics)
{
  Polygon90Set fill_area;
  fill_area += area;
  const bool is_horiz = layer->getDirection() == dbTechLayerDir::HORIZONTAL;
  const auto [space_x, space_y] = getSpacing(layer, config);
  std::vector<Rectangle> candidates;
  auto iter = config.shapes.begin();
  while (iter != config.shapes.end()) {
    auto [width, height] = *iter++;
    if ((is_horiz && width < height) || (!is_horiz && height < width)) {
      std::swap(width, height);
    }
    if (candidateTraceEnabled()) {
      traceCandidate("shape begin width=" + std::to_string(width)
                     + " height=" + std::to_string(height));
    }
    traceCandidate("shape.copy begin");
    Polygon90Set pruned_fill_area = fill_area;
    traceCandidate("shape.copy end; shape.shrink begin");
    const int east_west_sizing = width / 2 - 1;
    const int north_south_sizing = height / 2 - 1;
    shrink(pruned_fill_area,
           east_west_sizing,
           east_west_sizing,
           north_south_sizing,
           north_south_sizing);
    traceCandidate("shape.shrink end; shape.bloat begin");
    bloat(pruned_fill_area,
          east_west_sizing,
          east_west_sizing,
          north_south_sizing,
          north_south_sizing);
    traceCandidate("shape.bloat end; shape.prune begin");
    prune(pruned_fill_area, layer, config, graphics);
    traceCandidate("shape.prune end");
    if (graphics != nullptr) {
      graphics->status("Fill Area for " + std::to_string(width) + " "
                       + std::to_string(height));
      graphics->drawPolygon90Set(pruned_fill_area);
    }

    Polygon90Set all_shape_fills;
    std::vector<Polygon90> sub_fill_areas;
    traceCandidate("shape.extract_polygons begin");
    pruned_fill_area.get(sub_fill_areas);
    traceCandidate("shape.extract_polygons end");
    for (const auto& sub_fill_area : sub_fill_areas) {
      if (candidateTraceEnabled()) {
        traceCandidate("subpolygon begin index="
                       + std::to_string(&sub_fill_area - sub_fill_areas.data())
                       + " count=" + std::to_string(sub_fill_areas.size()));
      }
      Rectangle bounds;
      traceCandidate("subpolygon.extents begin");
      extents(bounds, sub_fill_area);
      traceCandidate(
          "subpolygon.extents end; subpolygon.tile_rectangles begin");
      Polygon90Set tiled_fills;
      for (int x = xl(bounds); x < xh(bounds); x += width + space_x) {
        for (int y = yl(bounds); y < yh(bounds); y += height + space_y) {
          tiled_fills.insert(makeRect(x, y, x + width, y + height));
        }
      }
      traceCandidate(
          "subpolygon.tile_rectangles end; subpolygon.intersect begin");
      Polygon90Set fills = tiled_fills & sub_fill_area;
      traceCandidate("subpolygon.intersect end; subpolygon.keep begin");
      keep(fills,
           width * height,
           width * height,
           width - 1,
           width,
           height - 1,
           height);
      traceCandidate("subpolygon.keep end; subpolygon.bloat_accumulate begin");
      Polygon90Set bloated_fills(fills);
      all_shape_fills
          += bloat(bloated_fills, space_x, space_x, space_y, space_y);
      traceCandidate(
          "subpolygon.bloat_accumulate end; subpolygon.get_rectangles begin");

      std::vector<Rectangle> rectangles;
      fills.get_rectangles(rectangles);
      traceCandidate("subpolygon.get_rectangles end; subpolygon.append begin");
      candidates.insert(candidates.end(), rectangles.begin(), rectangles.end());
      traceCandidate("subpolygon.append end; subpolygon end");
    }
    traceCandidate("shape.subtract_used begin");
    fill_area -= all_shape_fills;
    traceCandidate("shape.subtract_used end; shape end");
  }
  return candidates;
}

std::vector<Rectangle> makeTileFillCandidates(const Rect& tile,
                                              const Polygon90Set& non_fill,
                                              dbTechLayer* layer,
                                              const FillShapesConfig& config,
                                              Graphics* graphics,
                                              Polygon90Set* fillable_area)
{
  traceCandidate("tile.make_fill_area begin");
  Polygon90Set tile_area
      = makeTileFillArea(tile, non_fill, layer, config, graphics);
  traceCandidate("tile.make_fill_area end; tile.copy_fillable_area begin");
  if (fillable_area != nullptr) {
    *fillable_area = tile_area;
  }
  traceCandidate("tile.copy_fillable_area end; tile.extract_polygons begin");

  return makeTileFillCandidates(tile_area, layer, config, graphics);
}

std::vector<Rectangle> makeTileFillCandidates(const Polygon90Set& tile_area,
                                              dbTechLayer* layer,
                                              const FillShapesConfig& config,
                                              Graphics* graphics)
{
  traceCandidate("tile.extract_polygons begin");

  std::vector<Polygon90> polygons;
  tile_area.get(polygons);
  traceCandidate("tile.extract_polygons end");
  std::vector<Rectangle> candidates;
  for (const Polygon90& polygon : polygons) {
    if (candidateTraceEnabled()) {
      traceCandidate("polygon begin index="
                     + std::to_string(&polygon - polygons.data())
                     + " count=" + std::to_string(polygons.size()));
    }
    std::vector<Rectangle> polygon_candidates
        = makeFillCandidates(polygon, layer, config, graphics);
    candidates.insert(
        candidates.end(), polygon_candidates.begin(), polygon_candidates.end());
    traceCandidate("polygon end");
  }
  return candidates;
}

Polygon90Set makeTileFillArea(const Rect& tile,
                              const Polygon90Set& non_fill,
                              dbTechLayer* layer,
                              const FillShapesConfig& config,
                              Graphics* graphics)
{
  const auto [space_x, space_y] = getSpacing(layer, config);
  Polygon90Set tile_area;
  tile_area += makeRect(tile.xMin(), tile.yMin(), tile.xMax(), tile.yMax());

  // Keep fills from adjacent tiles apart without needing cross-tile conflicts.
  traceCandidate("tile_area.shrink begin");
  shrink(tile_area, space_x, space_x, space_y, space_y);
  traceCandidate("tile_area.shrink end; tile_area.subtract_non_fill begin");
  tile_area -= non_fill + config.space_to_non_fill;
  traceCandidate("tile_area.subtract_non_fill end; tile_area.prune begin");
  prune(tile_area, layer, config, graphics);
  traceCandidate("tile_area.prune end");
  return tile_area;
}

}  // namespace fin::lpfill

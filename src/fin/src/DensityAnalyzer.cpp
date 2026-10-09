// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "DensityAnalyzer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

#include "TileGrid.h"

namespace fin {
namespace {
using Rectangle = boost::polygon::rectangle_data<int>;
using Polygon90Set = boost::polygon::polygon_90_set_data<int>;
using boost::polygon::operators::operator&;
using boost::polygon::operators::operator+=;
Rectangle makeRectangle(const odb::Rect& rect)
{
  return Rectangle(rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax());
}

}  // namespace

std::pair<double, double> getWindowDensityRange(
    const std::vector<DensityWindow>& windows)
{
  if (windows.empty()) {
    return {0.0, 0.0};
  }

  double min_density = windows.front().density;
  double max_density = min_density;
  for (const DensityWindow& window : windows) {
    min_density = std::min(min_density, window.density);
    max_density = std::max(max_density, window.density);
  }
  return {min_density, max_density};
}

namespace {

FloatingDensityResult analyzeFloatingDensityInternal(
    const std::vector<odb::Rect>& rectangles,
    const odb::Rect& region,
    const int window_size,
    const bool use_spatial_index)
{
  if (window_size <= 0 || window_size > region.dx()
      || window_size > region.dy()) {
    throw std::invalid_argument(
        "Floating density window_size must fit inside the region.");
  }

  const int min_x = region.xMin();
  const int max_x = region.xMax() - window_size;
  const int min_y = region.yMin();
  const int max_y = region.yMax() - window_size;
  std::vector<odb::Rect> clipped_rectangles;
  clipped_rectangles.reserve(rectangles.size());
  for (const odb::Rect& rectangle : rectangles) {
    const odb::Rect clipped = rectangle.intersect(region);
    if (clipped.dx() > 0 && clipped.dy() > 0) {
      clipped_rectangles.push_back(clipped);
    }
  }

  // J40 ALG3's fixed-dissection preprocessing associates each rectangle
  // with the window-sized spatial cells it intersects.  A horizontal band
  // of height window_size intersects at most two such rows, so a y sweep
  // only visits rectangles from those rows instead of rescanning all k
  // rectangles.  The subsequent edge-event sweep is still exact.
  const size_t bucket_row_count
      = (static_cast<size_t>(region.dy()) + window_size - 1) / window_size;
  std::vector<std::vector<size_t>> bucket_rows(bucket_row_count);
  if (use_spatial_index) {
    for (size_t index = 0; index < clipped_rectangles.size(); index++) {
      const odb::Rect& rectangle = clipped_rectangles[index];
      const size_t first_row = (rectangle.yMin() - min_y) / window_size;
      const size_t last_row = (rectangle.yMax() - 1 - min_y) / window_size;
      for (size_t row = first_row; row <= last_row; row++) {
        bucket_rows[row].push_back(index);
      }
    }
  }
  std::vector<size_t> all_rectangle_indices(clipped_rectangles.size());
  std::iota(all_rectangle_indices.begin(), all_rectangle_indices.end(), 0);
  std::vector<size_t> rectangle_seen(clipped_rectangles.size(), 0);
  size_t seen_generation = 0;
  const auto rectangles_for_y = [&](const int y) {
    if (!use_spatial_index) {
      return all_rectangle_indices;
    }
    std::vector<size_t> indices;
    ++seen_generation;
    if (seen_generation == 0) {
      std::fill(rectangle_seen.begin(), rectangle_seen.end(), 0);
      ++seen_generation;
    }
    const size_t first_row = (y - min_y) / window_size;
    const size_t last_row = (y + window_size - 1 - min_y) / window_size;
    for (size_t row = first_row; row <= last_row; row++) {
      for (const size_t index : bucket_rows[row]) {
        if (rectangle_seen[index] == seen_generation) {
          continue;
        }
        rectangle_seen[index] = seen_generation;
        const odb::Rect& rectangle = clipped_rectangles[index];
        if (rectangle.yMin() < y + window_size && y < rectangle.yMax()) {
          indices.push_back(index);
        }
      }
    }
    return indices;
  };

  // J40 Theorem 3 ensures that an extremal window abuts feature edges.  The
  // x events are independent of y, so sorting them once yields O(k^2) work.
  std::vector<int> x_events;
  x_events.reserve(clipped_rectangles.size() * 4 + 2);
  x_events.push_back(min_x);
  x_events.push_back(max_x);
  std::vector<int> y_candidates{min_y, max_y};
  y_candidates.reserve(clipped_rectangles.size() * 4 + 2);
  for (const odb::Rect& rectangle : clipped_rectangles) {
    x_events.push_back(rectangle.xMin() - window_size);
    x_events.push_back(rectangle.xMax() - window_size);
    x_events.push_back(rectangle.xMin());
    x_events.push_back(rectangle.xMax());
    y_candidates.push_back(rectangle.yMin() - window_size);
    y_candidates.push_back(rectangle.yMax() - window_size);
    y_candidates.push_back(rectangle.yMin());
    y_candidates.push_back(rectangle.yMax());
  }
  const auto in_x_range = [min_x, max_x](const int value) {
    return value >= min_x && value <= max_x;
  };
  const auto in_y_range = [min_y, max_y](const int value) {
    return value >= min_y && value <= max_y;
  };
  x_events.erase(std::remove_if(x_events.begin(),
                                x_events.end(),
                                [&in_x_range](const int value) {
                                  return !in_x_range(value);
                                }),
                 x_events.end());
  y_candidates.erase(std::remove_if(y_candidates.begin(),
                                    y_candidates.end(),
                                    [&in_y_range](const int value) {
                                      return !in_y_range(value);
                                    }),
                     y_candidates.end());
  std::sort(x_events.begin(), x_events.end());
  x_events.erase(std::unique(x_events.begin(), x_events.end()), x_events.end());
  x_events.erase(std::remove(x_events.begin(), x_events.end(), min_x),
                 x_events.end());
  std::unordered_map<int, size_t> x_event_indices;
  x_event_indices.reserve(x_events.size());
  for (size_t index = 0; index < x_events.size(); index++) {
    x_event_indices.emplace(x_events[index], index);
  }
  std::sort(y_candidates.begin(), y_candidates.end());
  y_candidates.erase(std::unique(y_candidates.begin(), y_candidates.end()),
                     y_candidates.end());

  const auto make_x_events = [&](const std::vector<size_t>& indices) {
    std::vector<int> events;
    events.reserve(indices.size() * 4 + 1);
    events.push_back(max_x);
    for (const size_t index : indices) {
      const odb::Rect& rectangle = clipped_rectangles[index];
      for (const int x : {rectangle.xMin() - window_size,
                          rectangle.xMax() - window_size,
                          rectangle.xMin(),
                          rectangle.xMax()}) {
        if (x > min_x && x <= max_x) {
          events.push_back(x);
        }
      }
    }
    std::sort(events.begin(), events.end());
    events.erase(std::unique(events.begin(), events.end()), events.end());
    return events;
  };

  const double window_area
      = static_cast<double>(window_size) * static_cast<double>(window_size);
  FloatingDensityResult result{
      odb::Rect(min_x, min_y, min_x + window_size, min_y + window_size),
      odb::Rect(min_x, min_y, min_x + window_size, min_y + window_size),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      {}};

  for (const int y : y_candidates) {
    const std::vector<size_t> relevant_rectangles = rectangles_for_y(y);
    const std::vector<int> sweep_x_events
        = use_spatial_index ? make_x_events(relevant_rectangles) : x_events;
    std::unordered_map<int, size_t> sweep_x_event_indices;
    sweep_x_event_indices.reserve(sweep_x_events.size());
    for (size_t index = 0; index < sweep_x_events.size(); index++) {
      sweep_x_event_indices.emplace(sweep_x_events[index], index);
    }
    double area = 0.0;
    double slope = 0.0;
    std::vector<double> slope_deltas(sweep_x_events.size(), 0.0);
    const auto add_slope_delta = [&sweep_x_event_indices, &slope_deltas](
                                     const int x, const double delta) {
      const auto event = sweep_x_event_indices.find(x);
      if (event != sweep_x_event_indices.end()) {
        slope_deltas[event->second] += delta;
      }
    };
    for (const size_t rectangle_index : relevant_rectangles) {
      const odb::Rect& rectangle = clipped_rectangles[rectangle_index];
      const int overlap_y = std::max(0,
                                     std::min(y + window_size, rectangle.yMax())
                                         - std::max(y, rectangle.yMin()));
      if (overlap_y == 0) {
        continue;
      }
      const int overlap_x
          = std::max(0,
                     std::min(min_x + window_size, rectangle.xMax())
                         - std::max(min_x, rectangle.xMin()));
      area += static_cast<double>(overlap_x) * overlap_y;
      if (min_x >= rectangle.xMin() - window_size
          && min_x < rectangle.xMax() - window_size) {
        slope += overlap_y;
      }
      if (min_x >= rectangle.xMin() && min_x < rectangle.xMax()) {
        slope -= overlap_y;
      }
      add_slope_delta(rectangle.xMin() - window_size, overlap_y);
      add_slope_delta(rectangle.xMax() - window_size, -overlap_y);
      add_slope_delta(rectangle.xMin(), -overlap_y);
      add_slope_delta(rectangle.xMax(), overlap_y);
    }

    const auto record_density = [&result, window_area, window_size, y](
                                    const int x, const double window_area_sum) {
      const double density = window_area_sum / window_area;
      const odb::Rect window(x, y, x + window_size, y + window_size);
      if (density < result.min_density) {
        result.min_density = density;
        result.min_window = window;
      }
      if (density > result.max_density) {
        result.max_density = density;
        result.max_window = window;
      }
    };
    record_density(min_x, area);

    int previous_x = min_x;
    for (size_t event_index = 0; event_index < sweep_x_events.size();
         event_index++) {
      const int x = sweep_x_events[event_index];
      area += slope * (x - previous_x);
      record_density(x, area);
      slope += slope_deltas[event_index];
      previous_x = x;
    }
  }

  const auto add_profile = [&clipped_rectangles,
                            &result,
                            &rectangles_for_y,
                            &make_x_events,
                            &x_events,
                            min_x,
                            window_area,
                            window_size,
                            use_spatial_index](const int y) {
    FloatingDensityResult::Profile profile;
    profile.y = y;
    const std::vector<size_t> relevant_rectangles = rectangles_for_y(y);
    const std::vector<int> profile_x_events
        = use_spatial_index ? make_x_events(relevant_rectangles) : x_events;
    const auto add_point = [&clipped_rectangles,
                            &profile,
                            &relevant_rectangles,
                            window_area,
                            window_size,
                            y](const int x) {
      const odb::Rect window(x, y, x + window_size, y + window_size);
      double area = 0.0;
      for (const size_t index : relevant_rectangles) {
        const odb::Rect overlap = clipped_rectangles[index].intersect(window);
        if (overlap.dx() > 0 && overlap.dy() > 0) {
          area += static_cast<double>(overlap.dx()) * overlap.dy();
        }
      }
      profile.points.emplace_back(x, area / window_area);
    };
    add_point(min_x);
    for (const int x : profile_x_events) {
      add_point(x);
    }
    result.profiles.push_back(std::move(profile));
  };
  // Keep fifty evenly spaced y profiles, including the lower and upper
  // legal window positions.  Every profile remains exact in x by evaluating
  // all relevant rectangle edge events.
  constexpr int kProfileYCount = 50;
  for (int profile_index = 0; profile_index < kProfileYCount; profile_index++) {
    const int y = min_y
                  + static_cast<int>(static_cast<int64_t>(max_y - min_y)
                                     * profile_index / (kProfileYCount - 1));
    const bool already_sampled
        = std::any_of(result.profiles.begin(),
                      result.profiles.end(),
                      [y](const auto& profile) { return profile.y == y; });
    if (!already_sampled) {
      add_profile(y);
    }
  }
  return result;
}

}  // namespace

FloatingDensityResult analyzeFloatingDensityAlg2(
    const std::vector<odb::Rect>& rectangles,
    const odb::Rect& region,
    const int window_size)
{
  return analyzeFloatingDensityInternal(rectangles, region, window_size, false);
}

FloatingDensityResult analyzeFloatingDensityAlg3(
    const std::vector<odb::Rect>& rectangles,
    const odb::Rect& region,
    const int window_size)
{
  return analyzeFloatingDensityInternal(rectangles, region, window_size, true);
}

void TileGrid::calculateMetalDensities(
    const boost::polygon::polygon_90_set_data<int>& metal_shapes)
{
  metal_densities_.clear();
  metal_densities_.reserve(tiles_.size());
  metal_areas_.clear();
  metal_areas_.reserve(tiles_.size());

  for (const odb::Rect& tile : tiles_) {
    Polygon90Set tile_polygon;
    tile_polygon.insert(makeRectangle(tile));
    Polygon90Set covered_area = metal_shapes & tile_polygon;

    const double tile_area = static_cast<double>(tile.area());
    const double metal_area = boost::polygon::area(covered_area);
    metal_areas_.push_back(metal_area);
    metal_densities_.push_back(tile_area == 0.0 ? 0.0 : metal_area / tile_area);
  }

  calculateWindowDensities();
}

void TileGrid::calculateWindowDensities()
{
  if (tile_columns_ == 0) {
    return;
  }

  const size_t tile_rows = tiles_.size() / tile_columns_;
  const size_t prefix_columns = tile_columns_ + 1;
  std::vector<double> prefix((tile_rows + 1) * prefix_columns, 0.0);
  const auto prefixAt
      = [&prefix, prefix_columns](size_t row, size_t column) -> double& {
    return prefix[row * prefix_columns + column];
  };

  for (size_t row = 1; row <= tile_rows; row++) {
    for (size_t column = 1; column <= tile_columns_; column++) {
      prefixAt(row, column)
          = metal_areas_[(row - 1) * tile_columns_ + column - 1]
            + prefixAt(row - 1, column) + prefixAt(row, column - 1)
            - prefixAt(row - 1, column - 1);
    }
  }

  for (DensityWindow& window : windows_) {
    const size_t first_x = window.first_tile_x;
    const size_t first_y = window.first_tile_y;
    const size_t last_x = first_x + tiles_per_window_;
    const size_t last_y = first_y + tiles_per_window_;
    window.metal_area = prefixAt(last_y, last_x) - prefixAt(first_y, last_x)
                        - prefixAt(last_y, first_x)
                        + prefixAt(first_y, first_x);
    const double window_area = static_cast<double>(window.bounds.area());
    window.density = window_area == 0.0 ? 0.0 : window.metal_area / window_area;
    window.planned_fill_area = 0.0;
    window.post_fill_density = 0.0;
  }
}

double TileGrid::totalMetalArea() const
{
  double area = 0.0;
  for (const double metal_area : metal_areas_) {
    area += metal_area;
  }
  return area;
}

WindowDensityHistogram makeWindowDensityHistogram(
    const std::vector<DensityWindow>& windows)
{
  WindowDensityHistogram histogram{};
  for (const DensityWindow& window : windows) {
    const double density = std::clamp(window.density, 0.0, 1.0);
    const size_t bin
        = std::min(static_cast<size_t>(density * WindowDensityHistogramBins),
                   WindowDensityHistogramBins - 1);
    histogram[bin]++;
  }
  return histogram;
}

WindowDensitySummary summarizeWindowDensities(
    const std::vector<DensityWindow>& windows,
    double min_density_limit,
    double max_density_limit)
{
  WindowDensitySummary summary;
  if (windows.empty()) {
    return summary;
  }

  summary.min_density = windows.front().density;
  summary.max_density = windows.front().density;
  for (const DensityWindow& window : windows) {
    summary.min_density = std::min(summary.min_density, window.density);
    summary.max_density = std::max(summary.max_density, window.density);
    summary.mean_density += window.density;
    summary.min_violation_count += min_density_limit >= 0.0
                                   && window.density < min_density_limit - 1e-9;
    summary.max_violation_count += max_density_limit >= 0.0
                                   && window.density > max_density_limit + 1e-9;
  }
  summary.mean_density /= windows.size();
  for (const DensityWindow& window : windows) {
    const double delta = window.density - summary.mean_density;
    summary.variance += delta * delta;
  }
  summary.variance /= windows.size();
  return summary;
}
}  // namespace fin

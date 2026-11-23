// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillCheck.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#include "FillCommon.h"

namespace fin {
namespace {
std::vector<int> makeEdges(const int low,
                           const int high,
                           const int size,
                           const int step)
{
  if (low >= high || size <= 0 || step <= 0 || int64_t{low} + size > high) {
    throw std::invalid_argument(
        "Positive density windows and steps must fit inside the core.");
  }
  std::vector<int> edges{low, high};
  for (int64_t start = low; start + size <= high; start += step) {
    edges.push_back(start);
    edges.push_back(start + size);
  }
  std::ranges::sort(edges);
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  return edges;
}

size_t edgeIndex(const std::vector<int>& edges, const int coordinate)
{
  return std::lower_bound(edges.begin(), edges.end(), coordinate)
         - edges.begin();
}
}  // namespace

FillGrid::FillGrid(const odb::Rect& region,
                   const odb::Point& window_size,
                   const odb::Point& window_step)
    : x_edges_(makeEdges(region.xMin(),
                         region.xMax(),
                         window_size.x(),
                         window_step.x())),
      y_edges_(makeEdges(region.yMin(),
                         region.yMax(),
                         window_size.y(),
                         window_step.y()))
{
  const size_t columns = x_edges_.size() - 1;
  const size_t rows = y_edges_.size() - 1;
  if (columns > std::numeric_limits<int>::max() / rows) {
    throw std::invalid_argument(
        "Density grid exceeds the LP solver's index range.");
  }
  tiles_.reserve(columns * rows);
  for (size_t row = 0; row < rows; ++row) {
    for (size_t col = 0; col < columns; ++col) {
      tiles_.emplace_back(
          x_edges_[col], y_edges_[row], x_edges_[col + 1], y_edges_[row + 1]);
    }
  }
  for (int64_t y = region.yMin(); y + window_size.y() <= region.yMax();
       y += window_step.y()) {
    for (int64_t x = region.xMin(); x + window_size.x() <= region.xMax();
         x += window_step.x()) {
      FillWindow window{
          odb::Rect(x, y, x + window_size.x(), y + window_size.y()), {}};
      const size_t left = edgeIndex(x_edges_, x);
      const size_t right = edgeIndex(x_edges_, x + window_size.x());
      const size_t bottom = edgeIndex(y_edges_, y);
      const size_t top = edgeIndex(y_edges_, y + window_size.y());
      for (size_t row = bottom; row < top; ++row) {
        for (size_t col = left; col < right; ++col) {
          window.tiles.push_back(row * columns + col);
        }
      }
      windows_.push_back(std::move(window));
    }
  }
}

const std::vector<odb::Rect>& FillGrid::tiles() const
{
  return tiles_;
}

const std::vector<FillWindow>& FillGrid::windows() const
{
  return windows_;
}

std::vector<Polygon90Set> FillGrid::partition(
    const std::vector<Rectangle>& rectangles) const
{
  const size_t columns = x_edges_.size() - 1;
  std::vector<Polygon90Set> result(tiles_.size());
  for (const auto& rectangle : rectangles) {
    const int left = std::max(xl(rectangle), x_edges_.front());
    const int bottom = std::max(yl(rectangle), y_edges_.front());
    const int right = std::min(xh(rectangle), x_edges_.back());
    const int top = std::min(yh(rectangle), y_edges_.back());
    if (left >= right || bottom >= top) {
      continue;
    }
    const size_t first_x
        = std::upper_bound(x_edges_.begin(), x_edges_.end(), left)
          - x_edges_.begin() - 1;
    const size_t last_x = edgeIndex(x_edges_, right);
    const size_t first_y
        = std::upper_bound(y_edges_.begin(), y_edges_.end(), bottom)
          - y_edges_.begin() - 1;
    const size_t last_y = edgeIndex(y_edges_, top);
    for (size_t row = first_y; row < last_y; ++row) {
      for (size_t col = first_x; col < last_x; ++col) {
        result[row * columns + col].insert(
            Rectangle(std::max(left, x_edges_[col]),
                      std::max(bottom, y_edges_[row]),
                      std::min(right, x_edges_[col + 1]),
                      std::min(top, y_edges_[row + 1])));
      }
    }
  }
  return result;
}

std::vector<double> FillGrid::metalAreas(
    const std::vector<Rectangle>& rectangles,
    const int num_threads) const
{
  auto parts = partition(rectangles);
  std::vector<double> areas(parts.size());
  forEachFillTile(parts.size(), num_threads, [&](const size_t i) {
    areas[i] = boost::polygon::area(parts[i]);
  });
  return areas;
}

std::vector<double> FillGrid::windowDensities(
    const std::vector<double>& tile_metal_areas) const
{
  if (tile_metal_areas.size() != tiles_.size()) {
    throw std::invalid_argument(
        "Density tile area count does not match the grid.");
  }
  std::vector<double> result;
  result.reserve(windows_.size());
  for (const auto& window : windows_) {
    double metal = 0.0;
    for (const size_t tile : window.tiles) {
      metal += tile_metal_areas[tile];
    }
    result.push_back(metal / static_cast<double>(window.bounds.area()));
  }
  return result;
}

FillDensitySummary summarizeFillDensity(const std::vector<double>& densities,
                                        const double minimum,
                                        const double maximum)
{
  FillDensitySummary summary;
  if (densities.empty()) {
    return summary;
  }
  summary.minimum = *std::ranges::min_element(densities);
  summary.maximum = *std::ranges::max_element(densities);
  summary.mean = std::accumulate(densities.begin(), densities.end(), 0.0)
                 / densities.size();
  for (const double density : densities) {
    summary.below_minimum += density < minimum;
    summary.above_maximum += density > maximum;
  }
  return summary;
}
}  // namespace fin

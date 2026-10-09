// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "TileGrid.h"

#include <numeric>
#include <stdexcept>

namespace fin {
namespace {
int alignedStart(int coordinate, int origin, int tile_size)
{
  const int offset = coordinate - origin;
  if (offset >= 0) {
    return origin + offset / tile_size * tile_size;
  }
  return origin - (-(offset + 1) / tile_size + 1) * tile_size;
}
}  // namespace

TileGrid::TileGrid(const TileGridConfig& config) : region_(config.region)
{
  if (config.resolution <= 0 || config.window_size < config.resolution
      || config.window_size % config.resolution != 0) {
    throw std::invalid_argument(
        "TileGrid requires window_size to be divisible by a positive "
        "resolution.");
  }

  const int tile_size = config.window_size / config.resolution;
  tiles_per_window_ = config.resolution;
  const int first_tile_y
      = alignedStart(config.region.yMin(), config.origin.y(), tile_size);
  const int first_tile_x
      = alignedStart(config.region.xMin(), config.origin.x(), tile_size);
  for (int y = first_tile_y; y < config.region.yMax(); y += tile_size) {
    for (int x = first_tile_x; x < config.region.xMax(); x += tile_size) {
      tiles_.emplace_back(std::max(x, config.region.xMin()),
                          std::max(y, config.region.yMin()),
                          std::min(x + tile_size, config.region.xMax()),
                          std::min(y + tile_size, config.region.yMax()));
    }
  }
  for (int x = first_tile_x; x < config.region.xMax(); x += tile_size) {
    tile_columns_++;
  }

  int first_window_y = first_tile_y;
  if (first_window_y < config.region.yMin()) {
    first_window_y += tile_size;
  }
  int first_window_x = first_tile_x;
  if (first_window_x < config.region.xMin()) {
    first_window_x += tile_size;
  }
  for (int y = first_window_y; y + config.window_size <= config.region.yMax();
       y += tile_size) {
    for (int x = first_window_x; x + config.window_size <= config.region.xMax();
         x += tile_size) {
      windows_.push_back(
          {odb::Rect(x, y, x + config.window_size, y + config.window_size),
           static_cast<size_t>((x - first_tile_x) / tile_size),
           static_cast<size_t>((y - first_tile_y) / tile_size)});
    }
  }
}

std::vector<std::vector<size_t>> TileGrid::windowTileIndices() const
{
  std::vector<size_t> indices(windows_.size());
  std::iota(indices.begin(), indices.end(), 0);
  return windowTileIndices(indices);
}

std::vector<std::vector<size_t>> TileGrid::windowTileIndices(
    const std::vector<size_t>& window_indices) const
{
  std::vector<std::vector<size_t>> window_tiles;
  window_tiles.reserve(window_indices.size());
  for (const size_t window_index : window_indices) {
    if (window_index >= windows_.size()) {
      throw std::invalid_argument("A window index is invalid.");
    }
    const DensityWindow& window = windows_[window_index];
    std::vector<size_t> tile_indices;
    tile_indices.reserve(tiles_per_window_ * tiles_per_window_);
    for (size_t row = window.first_tile_y;
         row < window.first_tile_y + tiles_per_window_;
         row++) {
      for (size_t column = window.first_tile_x;
           column < window.first_tile_x + tiles_per_window_;
           column++) {
        tile_indices.push_back(row * tile_columns_ + column);
      }
    }
    window_tiles.push_back(std::move(tile_indices));
  }
  return window_tiles;
}

}  // namespace fin

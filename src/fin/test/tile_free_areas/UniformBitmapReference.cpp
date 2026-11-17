// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "UniformBitmapReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace fin::lpfill::bitmap_reference {
namespace {
using Coordinate = std::int64_t;
using Site = std::pair<Coordinate, Coordinate>;
struct SiteHash
{
  size_t operator()(const Site& site) const
  {
    const auto first = std::hash<Coordinate>{}(site.first);
    const auto second = std::hash<Coordinate>{}(site.second);
    return first ^ (second + 0x9e3779b9 + (first << 6) + (first >> 2));
  }
};
struct Entry
{
  size_t index;
  Site site;
};
using GroupKey = std::tuple<Coordinate, Coordinate, Coordinate, Coordinate>;
struct Group
{
  std::vector<Entry> entries;
  Coordinate min_x = std::numeric_limits<Coordinate>::max();
  Coordinate min_y = std::numeric_limits<Coordinate>::max();
  Coordinate max_x = std::numeric_limits<Coordinate>::min();
  Coordinate max_y = std::numeric_limits<Coordinate>::min();
};

Coordinate positiveRemainder(Coordinate value, Coordinate pitch)
{
  const auto remainder = value % pitch;
  return remainder < 0 ? remainder + pitch : remainder;
}

std::vector<Group> groupCandidates(const std::vector<Rectangle>& candidates,
                                   int space_x,
                                   int space_y)
{
  std::map<GroupKey, size_t> group_ids;
  std::vector<Group> groups;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto& rect = candidates[i];
    const Coordinate width = Coordinate(xh(rect)) - xl(rect);
    const Coordinate height = Coordinate(yh(rect)) - yl(rect);
    const Coordinate pitch_x = width + space_x, pitch_y = height + space_y;
    if (width <= 0 || height <= 0 || pitch_x <= 0 || pitch_y <= 0) {
      throw std::invalid_argument(
          "Uniform selection requires positive sizes/pitches.");
    }
    const auto rx = positiveRemainder(xl(rect), pitch_x);
    const auto ry = positiveRemainder(yl(rect), pitch_y);
    const GroupKey key{width, height, rx, ry};
    const auto [it, inserted] = group_ids.emplace(key, groups.size());
    if (inserted) {
      groups.emplace_back();
    }
    auto& group = groups[it->second];
    const Site site{(Coordinate(xl(rect)) - rx) / pitch_x,
                    (Coordinate(yl(rect)) - ry) / pitch_y};
    group.entries.push_back({i, site});
    group.min_x = std::min(group.min_x, site.first);
    group.min_y = std::min(group.min_y, site.second);
    group.max_x = std::max(group.max_x, site.first);
    group.max_y = std::max(group.max_y, site.second);
  }
  return groups;
}

using Sites = std::unordered_set<Site, SiteHash>;

int interiorDepth(const Sites& sites, const Site& site)
{
  // Two eight-neighbor erosions form three disjoint interior/boundary bands.
  for (int radius = 1; radius <= 2; ++radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (sites.count({site.first + dx, site.second + dy}) == 0) {
          return radius - 1;
        }
      }
    }
  }
  return 2;
}

using Bands = std::array<std::vector<Entry>, 12>;

Bands hashPriorityBands(const Group& group)
{
  Sites sites;
  sites.reserve(group.entries.size());
  for (const auto& entry : group.entries) {
    if (!sites.insert(entry.site).second) {
      throw std::invalid_argument("Duplicate candidate in uniform selection.");
    }
  }
  Bands bands;
  for (const auto& entry : group.entries) {
    const auto x = entry.site.first - group.min_x;
    const auto y = entry.site.second - group.min_y;
    const int parity = (x % 2) * 2 + y % 2;
    bands[(2 - interiorDepth(sites, entry.site)) * 4 + parity].push_back(entry);
  }
  for (auto& band : bands) {
    std::sort(band.begin(), band.end(), [](const Entry& a, const Entry& b) {
      return std::tie(a.site.second, a.site.first, a.index)
             < std::tie(b.site.second, b.site.first, b.index);
    });
  }
  return bands;
}

bool useBitmap(const Group& group)
{
  // Two empty border cells per side preserve the existing outside-is-empty
  // rule. Cap per-worker dense buffers and avoid scanning large empty grids.
  constexpr size_t max_cells = 1024 * 1024;
  const auto columns
      = static_cast<std::uint64_t>(group.max_x - group.min_x + 5);
  const auto rows = static_cast<std::uint64_t>(group.max_y - group.min_y + 5);
  if (columns > max_cells || rows > max_cells / columns) {
    return false;
  }
  return columns * rows <= group.entries.size() * 8ULL + 64;
}

Bands bitmapPriorityBands(const Group& group)
{
  const size_t columns = group.max_x - group.min_x + 5;
  const size_t rows = group.max_y - group.min_y + 5;
  const size_t no_index = std::numeric_limits<size_t>::max();
  // Byte-addressed availability and first erosion, plus a source-index map.
  // Each group is released before the next one; no 12 full bitmap copies.
  std::vector<std::uint8_t> bitmap(columns * rows), eroded(columns * rows);
  std::vector<size_t> indices(columns * rows, no_index);
  const auto position = [&](const Site& site) {
    return (site.second - group.min_y + 2) * columns + site.first - group.min_x
           + 2;
  };
  for (const auto& entry : group.entries) {
    const size_t pos = position(entry.site);
    if (bitmap[pos]) {
      throw std::invalid_argument("Duplicate candidate in uniform selection.");
    }
    bitmap[pos] = 1;
    indices[pos] = entry.index;
  }
  const std::ptrdiff_t stride = columns;
  const std::array<std::ptrdiff_t, 8> neighbors{
      -stride - 1, -stride, -stride + 1, -1, 1, stride - 1, stride, stride + 1};
  const auto surrounded = [&](const auto& cells, size_t pos) {
    return std::all_of(neighbors.begin(), neighbors.end(), [&](auto offset) {
      return cells[static_cast<std::ptrdiff_t>(pos) + offset] != 0;
    });
  };
  for (const auto& entry : group.entries) {
    const size_t pos = position(entry.site);
    eroded[pos] = surrounded(bitmap, pos);
  }
  Bands bands;
  // Row-major scanning directly yields the old sorted order within each band.
  for (size_t row = 2; row + 2 < rows; ++row) {
    for (size_t col = 2; col + 2 < columns; ++col) {
      const size_t pos = row * columns + col;
      if (!bitmap[pos]) {
        continue;
      }
      const int depth = eroded[pos] ? 1 + surrounded(eroded, pos) : 0;
      const int parity = ((col - 2) % 2) * 2 + (row - 2) % 2;
      bands[(2 - depth) * 4 + parity].push_back(
          {indices[pos],
           {static_cast<Coordinate>(col - 2) + group.min_x,
            static_cast<Coordinate>(row - 2) + group.min_y}});
    }
  }
  return bands;
}

Bands priorityBands(const Group& group,
                    UniformBackend backend,
                    UniformSelectionStats* stats)
{
  const bool bitmap = backend == UniformBackend::Auto && useBitmap(group);
  if (stats != nullptr) {
    bitmap ? ++stats->bitmap_groups : ++stats->hash_groups;
  }
  return bitmap ? bitmapPriorityBands(group) : hashPriorityBands(group);
}
}  // namespace

double selectUniformCandidates(const std::vector<Rectangle>& candidates,
                               double target_area,
                               int space_x,
                               int space_y,
                               std::vector<Rectangle>& selected,
                               UniformBackend backend,
                               UniformSelectionStats* stats)
{
  if (!std::isfinite(target_area) || target_area < 0 || space_x < 0
      || space_y < 0) {
    throw std::invalid_argument("Invalid uniform selection target or spacing.");
  }
  // Match the area-deficit threshold of the regular bitmap selector in DBU^2.
  constexpr double deficit_tolerance = 0.001;
  double placed_area = 0.0;
  if (target_area <= deficit_tolerance) {
    return placed_area;
  }
  std::vector<bool> chosen(candidates.size());
  for (const auto& group : groupCandidates(candidates, space_x, space_y)) {
    for (const auto& band : priorityBands(group, backend, stats)) {
      for (const auto& entry : band) {
        if (target_area - placed_area <= deficit_tolerance) {
          break;
        }
        const auto& rect = candidates[entry.index];
        placed_area
            += (double(xh(rect)) - xl(rect)) * (double(yh(rect)) - yl(rect));
        chosen[entry.index] = true;
      }
    }
    if (target_area - placed_area <= deficit_tolerance) {
      break;
    }
  }
  // Preserve input order for DB IDs/masks, independently of decision priority.
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (chosen[i]) {
      selected.push_back(candidates[i]);
    }
  }
  return placed_area;
}
}  // namespace fin::lpfill::bitmap_reference

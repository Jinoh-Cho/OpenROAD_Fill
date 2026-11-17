// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "CandidateSelection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fin::lpfill {
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
using GroupKey = std::array<Coordinate, 4>;
struct GroupHash
{
  size_t operator()(const GroupKey& key) const
  {
    size_t hash = 0;
    for (const auto value : key) {
      hash ^= std::hash<Coordinate>{}(value) + 0x9e3779b9 + (hash << 6)
              + (hash >> 2);
    }
    return hash;
  }
};
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

template <typename Lookup>
std::vector<Group> groupCandidates(const std::vector<Rectangle>& candidates,
                                   int space_x,
                                   int space_y,
                                   Lookup& group_ids,
                                   bool cache_last,
                                   const FillPatternConfig& pattern)
{
  std::vector<Group> groups;
  GroupKey last_key{};
  size_t last_id = 0;
  bool has_last = false;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto& rect = candidates[i];
    const Coordinate width = Coordinate(xh(rect)) - xl(rect);
    const Coordinate height = Coordinate(yh(rect)) - yl(rect);
    const Coordinate pitch_x = width + space_x, pitch_y = height + space_y;
    if (width <= 0 || height <= 0 || pitch_x <= 0 || pitch_y <= 0) {
      throw std::invalid_argument(
          "Uniform selection requires positive sizes/pitches.");
    }
    const auto logical_x
        = pattern.type == FillPattern::Stagger
              ? unshiftPatternX(xl(rect), yl(rect), pitch_x, pitch_y, pattern)
              : Coordinate(xl(rect));
    const auto rx = positiveRemainder(logical_x, pitch_x);
    const auto ry = positiveRemainder(yl(rect), pitch_y);
    const GroupKey key{width, height, rx, ry};
    if (!cache_last || !has_last || key != last_key) {
      const auto [it, inserted] = group_ids.emplace(key, groups.size());
      if (inserted) {
        groups.emplace_back();
      }
      last_id = it->second;
      last_key = key;
      has_last = true;
    }
    auto& group = groups[last_id];
    const Site site{(logical_x - rx) / pitch_x,
                    (Coordinate(yl(rect)) - ry) / pitch_y};
    group.entries.push_back({i, site});
    group.min_x = std::min(group.min_x, site.first);
    group.min_y = std::min(group.min_y, site.second);
    group.max_x = std::max(group.max_x, site.first);
    group.max_y = std::max(group.max_y, site.second);
  }
  return groups;
}

std::vector<Group> groupCandidates(const std::vector<Rectangle>& candidates,
                                   int space_x,
                                   int space_y,
                                   bool fast_lookup,
                                   const FillPatternConfig& pattern)
{
  if (fast_lookup) {
    std::unordered_map<GroupKey, size_t, GroupHash> ids;
    ids.reserve(candidates.size() / 4 + 1);
    // The lookup container is never iterated: IDs remain first-appearance IDs.
    return groupCandidates(candidates, space_x, space_y, ids, true, pattern);
  }
  std::map<GroupKey, size_t> ids;
  return groupCandidates(candidates, space_x, space_y, ids, false, pattern);
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

struct PriorityScratch
{
  std::vector<std::uint8_t> bitmap, eroded;
  std::vector<size_t> indices;
  Bands bands;
  Sites sites;
};

void hashPriorityBands(const Group& group, PriorityScratch& scratch)
{
  auto& sites = scratch.sites;
  sites.clear();
  if (sites.bucket_count() < group.entries.size()) {
    sites.reserve(group.entries.size());
  }
  for (const auto& entry : group.entries) {
    if (!sites.insert(entry.site).second) {
      throw std::invalid_argument("Duplicate candidate in uniform selection.");
    }
  }
  auto& bands = scratch.bands;
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

void bitmapPriorityBands(const Group& group, PriorityScratch& scratch)
{
  const size_t columns = group.max_x - group.min_x + 5;
  const size_t rows = group.max_y - group.min_y + 5;
  // Byte-addressed availability and first erosion, plus a source-index map.
  // Capacity is reused within this tile; contents are reset for every group.
  auto& bitmap = scratch.bitmap;
  auto& eroded = scratch.eroded;
  auto& indices = scratch.indices;
  bitmap.assign(columns * rows, 0);
  eroded.assign(columns * rows, 0);
  indices.resize(columns * rows);
  const auto position = [&](const Site& site) {
    const size_t row = site.second - group.min_y + 2;
    const size_t col = site.first - group.min_x + 2;
    return row * columns + col;
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
  auto& bands = scratch.bands;
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
}

const Bands& priorityBands(const Group& group,
                           UniformBackend backend,
                           UniformSelectionStats* stats,
                           PriorityScratch& scratch)
{
  for (auto& band : scratch.bands) {
    band.clear();
  }
  const bool bitmap = backend == UniformBackend::Auto && useBitmap(group);
  if (stats != nullptr) {
    bitmap ? ++stats->bitmap_groups : ++stats->hash_groups;
  }
  if (bitmap) {
    bitmapPriorityBands(group, scratch);
  } else {
    hashPriorityBands(group, scratch);
  }
  return scratch.bands;
}

std::array<Entry, 4> smallGroupOrder(const Group& group)
{
  // Fewer than nine distinct sites cannot survive an eight-neighbor erosion.
  // Four-entry groups therefore need only parity/row sorting on the stack.
  std::array<Entry, 4> entries{};
  std::copy(group.entries.begin(), group.entries.end(), entries.begin());
  for (size_t i = 0; i < group.entries.size(); ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (entries[i].site == entries[j].site) {
        throw std::invalid_argument(
            "Duplicate candidate in uniform selection.");
      }
    }
  }
  const auto key = [&](const Entry& entry) {
    const auto x = entry.site.first - group.min_x;
    const auto y = entry.site.second - group.min_y;
    return std::tuple{
        (x % 2) * 2 + y % 2, entry.site.second, entry.site.first, entry.index};
  };
  std::sort(entries.begin(),
            entries.begin() + group.entries.size(),
            [&](const Entry& a, const Entry& b) { return key(a) < key(b); });
  return entries;
}
}  // namespace

SelectionPolicy parseSelectionPolicy(const std::string& name)
{
  if (name == "greedy") {
    return SelectionPolicy::Greedy;
  }
  if (name == "uniform") {
    return SelectionPolicy::Uniform;
  }
  throw std::invalid_argument("Selection policy must be greedy or uniform.");
}

double selectTileCandidates(const std::vector<Rectangle>& candidates,
                            double target_area,
                            SelectionPolicy policy,
                            int space_x,
                            int space_y,
                            std::vector<Rectangle>& selected,
                            const FillPatternConfig& pattern)
{
  if (policy == SelectionPolicy::Uniform) {
    return selectUniformCandidates(candidates,
                                   target_area,
                                   space_x,
                                   space_y,
                                   selected,
                                   UniformBackend::Auto,
                                   nullptr,
                                   {},
                                   pattern);
  }
  double placed_area = 0.0;
  for (const auto& candidate : candidates) {
    const double area = (double(xh(candidate)) - xl(candidate))
                        * (double(yh(candidate)) - yl(candidate));
    if (placed_area + area <= target_area) {
      selected.push_back(candidate);
      placed_area += area;
    }
  }
  return placed_area;
}

double selectUniformCandidates(const std::vector<Rectangle>& candidates,
                               double target_area,
                               int space_x,
                               int space_y,
                               std::vector<Rectangle>& selected,
                               UniformBackend backend,
                               UniformSelectionStats* stats,
                               const UniformOptimizations& optimizations,
                               const FillPatternConfig& pattern)
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
  const auto choose = [&](const Entry& entry) {
    const auto& rect = candidates[entry.index];
    placed_area
        += (double(xh(rect)) - xl(rect)) * (double(yh(rect)) - yl(rect));
    chosen[entry.index] = true;
  };
  PriorityScratch reused;
  for (const auto& group : groupCandidates(
           candidates, space_x, space_y, optimizations.group_lookup, pattern)) {
    if (optimizations.small_groups && group.entries.size() <= 4) {
      if (stats != nullptr) {
        ++stats->small_groups;
      }
      const auto entries = smallGroupOrder(group);
      for (size_t i = 0; i < group.entries.size(); ++i) {
        if (target_area - placed_area <= deficit_tolerance) {
          break;
        }
        choose(entries[i]);
      }
    } else {
      PriorityScratch temporary;
      auto& scratch = optimizations.reuse_buffers ? reused : temporary;
      for (const auto& band : priorityBands(group, backend, stats, scratch)) {
        for (const auto& entry : band) {
          if (target_area - placed_area <= deficit_tolerance) {
            break;
          }
          choose(entry);
        }
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
}  // namespace fin::lpfill

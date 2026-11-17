// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "UniformSelectionReference.h"

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

namespace fin::lpfill::selection_reference {
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
  std::unordered_set<Site, SiteHash> sites;
  Coordinate min_x = std::numeric_limits<Coordinate>::max();
  Coordinate min_y = std::numeric_limits<Coordinate>::max();
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
    if (!group.sites.insert(site).second) {
      throw std::invalid_argument("Duplicate candidate in uniform selection.");
    }
    group.entries.push_back({i, site});
    group.min_x = std::min(group.min_x, site.first);
    group.min_y = std::min(group.min_y, site.second);
  }
  return groups;
}

int interiorDepth(const Group& group, const Site& site)
{
  // Two eight-neighbor erosions form three disjoint interior/boundary bands.
  for (int radius = 1; radius <= 2; ++radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (group.sites.count({site.first + dx, site.second + dy}) == 0) {
          return radius - 1;
        }
      }
    }
  }
  return 2;
}

std::array<std::vector<Entry>, 12> priorityBands(const Group& group)
{
  std::array<std::vector<Entry>, 12> bands;
  for (const auto& entry : group.entries) {
    const auto x = entry.site.first - group.min_x;
    const auto y = entry.site.second - group.min_y;
    const int parity = (x % 2) * 2 + y % 2;
    bands[(2 - interiorDepth(group, entry.site)) * 4 + parity].push_back(entry);
  }
  for (auto& band : bands) {
    std::sort(band.begin(), band.end(), [](const Entry& a, const Entry& b) {
      return std::tie(a.site.second, a.site.first, a.index)
             < std::tie(b.site.second, b.site.first, b.index);
    });
  }
  return bands;
}
}  // namespace

double selectUniformCandidates(const std::vector<Rectangle>& candidates,
                               double target_area,
                               int space_x,
                               int space_y,
                               std::vector<Rectangle>& selected)
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
    for (const auto& band : priorityBands(group)) {
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
}  // namespace fin::lpfill::selection_reference

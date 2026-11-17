// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once

#include <string>
#include <vector>

#include "FillPattern.h"
#include "polygon.h"

namespace fin::lpfill {
enum class SelectionPolicy
{
  Greedy,
  Uniform
};

// Internal benchmark controls; the public uniform policy uses Auto.
enum class UniformBackend
{
  Auto,
  Hash
};
struct UniformSelectionStats
{
  size_t bitmap_groups = 0;
  size_t hash_groups = 0;
  size_t small_groups = 0;
};

// Internal ablation controls. Production enables all three optimizations.
struct UniformOptimizations
{
  bool small_groups = true;
  bool reuse_buffers = true;
  bool group_lookup = true;
};

SelectionPolicy parseSelectionPolicy(const std::string& name);

double selectTileCandidates(const std::vector<Rectangle>& candidates,
                            double target_area,
                            SelectionPolicy policy,
                            int space_x,
                            int space_y,
                            std::vector<Rectangle>& selected,
                            const FillPatternConfig& pattern = {});

// Tile-local adaptation of interior/sparse bitmap scheduling. Shape/grid
// groups retain first-appearance priority; missing neighboring sites are empty.
// A final rectangle may exceed the LP target. Returns actual selected area.
double selectUniformCandidates(const std::vector<Rectangle>& candidates,
                               double target_area,
                               int space_x,
                               int space_y,
                               std::vector<Rectangle>& selected,
                               UniformBackend backend = UniformBackend::Auto,
                               UniformSelectionStats* stats = nullptr,
                               const UniformOptimizations& optimizations = {},
                               const FillPatternConfig& pattern = {});
}  // namespace fin::lpfill

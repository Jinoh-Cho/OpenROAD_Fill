// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include "DensityAnalysis.h"

namespace fin {
// Return the minimum and maximum densities across all windows.  An empty
// collection has a density range of {0.0, 0.0}.
std::pair<double, double> getWindowDensityRange(
    const std::vector<DensityWindow>& windows);

// Run J40 ALG2 exact area-density analysis.  rectangles must be pairwise
// non-overlapping; shapes outside region are clipped before analysis.
FloatingDensityResult analyzeFloatingDensityAlg2(
    const std::vector<odb::Rect>& rectangles,
    const odb::Rect& region,
    int window_size);

// Run J40 ALG3 using window-sized spatial buckets to restrict each y sweep.
// The result is exact and has the same contract as ALG2.
FloatingDensityResult analyzeFloatingDensityAlg3(
    const std::vector<odb::Rect>& rectangles,
    const odb::Rect& region,
    int window_size);

WindowDensityHistogram makeWindowDensityHistogram(
    const std::vector<DensityWindow>& windows);
WindowDensitySummary summarizeWindowDensities(
    const std::vector<DensityWindow>& windows,
    double min_density_limit,
    double max_density_limit);

}  // namespace fin

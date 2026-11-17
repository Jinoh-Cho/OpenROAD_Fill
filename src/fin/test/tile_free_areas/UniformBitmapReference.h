// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once
#include "CandidateSelection.h"
namespace fin::lpfill::bitmap_reference {
// Frozen bitmap implementation before small-group/buffer/lookup optimization.
double selectUniformCandidates(const std::vector<Rectangle>& candidates,
                               double target_area,
                               int space_x,
                               int space_y,
                               std::vector<Rectangle>& selected,
                               UniformBackend backend = UniformBackend::Auto,
                               UniformSelectionStats* stats = nullptr);
}  // namespace fin::lpfill::bitmap_reference

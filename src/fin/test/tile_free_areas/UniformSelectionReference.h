// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once
#include <vector>

#include "polygon.h"
namespace fin::lpfill::selection_reference {
// Frozen pre-bitmap implementation, linked only into the test binary.
double selectUniformCandidates(const std::vector<Rectangle>& candidates,
                               double target_area,
                               int space_x,
                               int space_y,
                               std::vector<Rectangle>& selected);
}  // namespace fin::lpfill::selection_reference

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "FillUtill.h"
#include "FixedDissectionLp.h"

namespace fin {

enum class LipLpType
{
  kLip1 = 1,
  kLip2 = 2,
  kLip3 = 3
};

std::vector<std::vector<size_t>> makeLipLpNeighborhoods(const TileGrid& grid,
                                                        int resolution,
                                                        LipLpType type);

// Solve a fixed-dissection LP minimizing the largest density range among the
// supplied neighborhoods. Each neighborhood is a set of indices into
// FixedDissectionLpProblem::windows (for example, Lip-I row neighborhoods,
// Lip-II tile neighborhoods, or Lip-III local-square neighborhoods).
FixedDissectionLpResult solveLipLpFill(
    const FixedDissectionLpProblem& problem,
    const std::vector<std::vector<size_t>>& neighborhoods);

}  // namespace fin

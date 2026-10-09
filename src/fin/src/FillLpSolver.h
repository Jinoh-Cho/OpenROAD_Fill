// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include "FixedDissectionLp.h"

namespace fin {

enum class FillLpObjective
{
  MaximizeMinimumWindowArea,
  MinimizeTotalFillArea
};

// Shared OR-Tools model for fixed-dissection fill planners.  This is an
// implementation detail; algorithm-specific modules expose named entry points.
FixedDissectionLpResult solveFillLp(const FixedDissectionLpProblem& problem,
                                    FillLpObjective objective);

}  // namespace fin

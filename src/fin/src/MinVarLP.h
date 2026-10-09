// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include "FixedDissectionLp.h"

namespace fin {

// Maximize the minimum post-fill window area (J40 max-min objective).
// Receives numeric LP input; does not access the design DB or place rectangles.
FixedDissectionLpResult solveMinVarLP(const FixedDissectionLpProblem& problem);

}  // namespace fin

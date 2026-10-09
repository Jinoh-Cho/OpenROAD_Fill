// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include "FixedDissectionLp.h"

namespace fin {

// Minimize total placed fill subject to fixed-dissection density and capacity
// constraints.
FixedDissectionLpResult solveMinFillAmountLp(
    const FixedDissectionLpProblem& problem);

}  // namespace fin

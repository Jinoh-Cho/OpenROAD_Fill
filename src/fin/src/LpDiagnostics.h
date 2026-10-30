// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>

#include "FixedDissectionLp.h"

namespace operations_research {
class MPSolver;
}

namespace fin {
class TileGrid;

std::string lpJsonString(const std::string& value);
const char* fillLpStatusName(int status);
FixedDissectionLpResult recordFillLpFailure(
    const FixedDissectionLpProblem& problem,
    const operations_research::MPSolver& solver,
    int status,
    double solve_seconds,
    double model_area_scale = 1.0);

// Writes original and propagated bounds with a coordinate mapping. Bound
// propagation finds some conflicts, not a general irreducible infeasible set.
void writeFillLpFailureDiagnostics(const FixedDissectionLpProblem& problem,
                                   const TileGrid& grid,
                                   const char* layer,
                                   const FixedDissectionLpResult& result,
                                   double requested_min_window_density,
                                   const std::string& precheck_reason);
}  // namespace fin

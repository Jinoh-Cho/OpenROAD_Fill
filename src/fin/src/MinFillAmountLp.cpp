// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "MinFillAmountLp.h"

#include "FillLpSolver.h"

namespace fin {

FixedDissectionLpResult solveMinFillAmountLp(
    const FixedDissectionLpProblem& problem)
{
  return solveFillLp(problem, FillLpObjective::MinimizeTotalFillArea);
}

}  // namespace fin

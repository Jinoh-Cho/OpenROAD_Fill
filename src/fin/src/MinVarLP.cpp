// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "MinVarLP.h"

#include "FillLpSolver.h"

namespace fin {

FixedDissectionLpResult solveMinVarLP(const FixedDissectionLpProblem& problem)
{
  return solveFillLp(problem, FillLpObjective::MaximizeMinimumWindowArea);
}

}  // namespace fin

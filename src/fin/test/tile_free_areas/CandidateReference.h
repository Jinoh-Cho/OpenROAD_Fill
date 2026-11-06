// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once
#include "LPFillUtil.h"
namespace fin::lpfill::test_reference {
std::vector<Rectangle> makeFillCandidatesBefore(const Polygon90& area,
                                                odb::dbTechLayer* layer,
                                                const FillShapesConfig& config,
                                                Graphics* graphics);
std::vector<Rectangle> makeTileFillCandidatesBefore(
    const Polygon90Set& area,
    odb::dbTechLayer* layer,
    const FillShapesConfig& config,
    Graphics* graphics);
}  // namespace fin::lpfill::test_reference

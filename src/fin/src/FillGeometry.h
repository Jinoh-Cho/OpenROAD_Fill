// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include "odb/db.h"
#include "polygon.h"

namespace fin {

Polygon90 makeRect(int x_lo, int y_lo, int x_hi, int y_hi);

void insertShape(const odb::dbShape& shape,
                 Polygon90Set& polygon_set,
                 odb::dbTechLayer* layer);

// Collect regular and special wires, vias, instance pins, and obstructions.
Polygon90Set orNonFills(odb::dbBlock* block, odb::dbTechLayer* layer);

}  // namespace fin

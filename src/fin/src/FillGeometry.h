// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <map>
#include <string>
#include <vector>

#include "odb/db.h"
#include "polygon.h"

namespace fin {

Polygon90 makeRect(int x_lo, int y_lo, int x_hi, int y_hi);

void insertShape(const odb::dbShape& shape,
                 Polygon90Set& polygon_set,
                 odb::dbTechLayer* layer,
                 Polygon90Set* expanded = nullptr,
                 int spacing = 0,
                 std::vector<Rectangle>* expanded_rectangles = nullptr,
                 std::vector<Rectangle>* original_rectangles = nullptr);

// Collect regular and special wires, vias, instance pins, and obstructions.
// Optionally collect the union of positively expanded source rectangles too.
// Zero-area input rectangles fall back to the legacy expansion; spacing_fused
// reports whether direct rectangle expansion was used.
// original_rectangles optionally receives unexpanded source rectangles in the
// same pass, or legacy-normalized rectangles for degenerate source polygons.
Polygon90Set orNonFills(odb::dbBlock* block,
                        odb::dbTechLayer* layer,
                        std::map<std::string, double>* phase_seconds = nullptr,
                        int threads = 1,
                        int* actual_threads = nullptr,
                        Polygon90Set* expanded = nullptr,
                        int spacing = 0,
                        bool* spacing_fused = nullptr,
                        std::vector<Rectangle>* expanded_rectangles = nullptr,
                        std::vector<Rectangle>* original_rectangles = nullptr);

}  // namespace fin

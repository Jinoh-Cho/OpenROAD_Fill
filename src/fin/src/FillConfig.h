// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <utility>
#include <vector>

#include "odb/PtrSetMap.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace fin {

struct FillShapesConfig
{
  std::vector<std::pair<int, int>> shapes;
  int space_to_fill;
  int space_to_non_fill;
  int space_line_end;
};

struct FillLayerConfig
{
  int space_to_outline;
  int num_masks;
  int opc_halo;
  bool has_opc;
  FillShapesConfig opc;
  FillShapesConfig non_opc;
};

using FillLayerConfigs = odb::PtrMap<odb::dbTechLayer, FillLayerConfig>;

// Read a density-fill JSON rule file and expand layer groups into individual
// technology-layer configurations in DBU units.
FillLayerConfigs loadFillLayerConfigs(const char* filename,
                                      odb::dbTech* tech,
                                      utl::Logger* logger);

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#include "fin/Finale.h"

#include "DensityFill.h"
#include "FillCheck.h"
#include "LPFill.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace fin {

////////////////////////////////////////////////////////////////

Finale::Finale(odb::dbDatabase* db, utl::Logger* logger)
    : db_(db), logger_(logger)
{
}

void Finale::setDebug()
{
  debug_ = true;
}

void Finale::densityFill(const char* rules_filename, const odb::Rect& fill_area)
{
  DensityFill filler(db_, logger_, debug_);
  filler.fill(rules_filename, fill_area);
}

void Finale::lpFill(const LPFillOptions& options)
{
  LPFill filler(db_, logger_);
  filler.fill(options);
}

void Finale::discreteDensityCheck(const DiscreteDensityCheckOptions& options)
{
  checkDiscreteDensity(db_, logger_, options);
}

void Finale::continuousDensityCheck(
    const ContinuousDensityCheckOptions& options)
{
  checkContinuousDensity(db_, logger_, options);
}

}  // namespace fin

// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

%{
#include "ord/OpenRoad.hh"
#include "fin/Finale.h"

%}

%include "../../Exception.i"

%inline %{

void
set_density_fill_debug_cmd()
{
  auto *finale = ord::OpenRoad::openRoad()->getFinale();
  finale->setDebug();
}

void
density_fill_cmd(const char* rules_filename,
                 const odb::Rect& fill_area)
{
  auto *finale = ord::OpenRoad::openRoad()->getFinale();
  finale->densityFill(rules_filename, fill_area);
}

void
lp_fill_cmd(const char* rules_filename,
            const char* objective,
            const char* solver,
            int solver_threads,
            int num_threads,
            double window_width,
            double window_height,
            double step_x,
            double step_y,
            double min_density,
            double max_density,
            double min_tile_density,
            double max_tile_density)
{
  fin::LPFillOptions options;
  options.rules_file = rules_filename;
  options.objective = objective;
  options.solver = solver;
  options.solver_threads = solver_threads;
  options.num_threads = num_threads;
  options.window_width = window_width;
  options.window_height = window_height;
  options.step_x = step_x;
  options.step_y = step_y;
  options.min_density = min_density;
  options.max_density = max_density;
  options.min_tile_density = min_tile_density;
  options.max_tile_density = max_tile_density;
  ord::OpenRoad::openRoad()->getFinale()->lpFill(options);
}

%} // inline


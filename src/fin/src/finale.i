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
set_min_var_fill_debug_cmd()
{
  auto* finale = ord::OpenRoad::openRoad()->getFinale();
  finale->setMinVarDebug();
}

void
density_fill_cmd(const char* rules_filename,
                 const odb::Rect& fill_area)
{
  auto *finale = ord::OpenRoad::openRoad()->getFinale();
  finale->densityFill(rules_filename, fill_area);
}

void
min_var_fill_cmd(const char* rules_filename,
                 const odb::Rect& fill_area)
{
  auto* finale = ord::OpenRoad::openRoad()->getFinale();
  finale->minVarFill(rules_filename, fill_area);
}

void
density_fill_rectangle_extraction_benchmark_cmd(const char* rules_filename,
                                                 const odb::Rect& fill_area,
                                                 int left_copies,
                                                 int right_copies,
                                                 int bottom_copies,
                                                 int top_copies,
                                                 int runs)
{
  auto *finale = ord::OpenRoad::openRoad()->getFinale();
  finale->benchmarkRectangleExtraction(rules_filename,
                                       fill_area,
                                       left_copies,
                                       right_copies,
                                       bottom_copies,
                                       top_copies,
                                       runs);
}

double
tile_grid_metal_area_cmd(const char* rules_filename,
                         const odb::Rect& region,
                         const odb::Point& origin,
                         int window_size,
                         int resolution,
                         const char* svg_filename)
{
  auto* finale = ord::OpenRoad::openRoad()->getFinale();
  return finale->tileGridMetalArea(
      rules_filename, region, origin, window_size, resolution, svg_filename);
}

%} // inline

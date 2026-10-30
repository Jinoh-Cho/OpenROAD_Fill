// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "DensityAnalysis.h"

namespace utl {
class Logger;
}
namespace fin {
struct TileDensityReport
{
  std::string name;
  size_t window_count;
  double min_density;
  double max_density;
  double mean_density;
  double variance;
  size_t min_violation_window_count;
  size_t max_violation_window_count;
  WindowDensityHistogram histogram;
  double compute_seconds;
  double svg_seconds;
};

struct LPFillDensityReport
{
  std::string name;
  double planned_fill_area;
  double placed_fill_area;
  size_t window_count;
  double min_density;
  double max_density;
  double mean_density;
  double variance;
  size_t min_violation_window_count;
  size_t max_violation_window_count;
  WindowDensityHistogram histogram;
  bool solved;
  std::string infeasibility_reason;
  size_t diagnostic_window_index;
  odb::Rect diagnostic_window_bounds;
  double diagnostic_current_density;
  double diagnostic_required_fill_area;
  double diagnostic_max_legal_fill_area;
  double diagnostic_fill_budget;
  size_t tile_violation_count;
  size_t window_violation_count;
  double candidate_seconds;
  double solve_seconds;
  double placement_seconds;
  double compute_seconds;
  double svg_seconds;
  std::string solver_status;
  int solver_status_code = -1;
  std::string diagnostics_prefix;
  double diagnostics_seconds = 0.0;
  double candidate_generation_seconds = 0.0;
  double candidate_generation_work_seconds = 0.0;
  int candidate_threads = 1;
  int candidate_prepare_threads = 1;
  double placement_prepare_seconds = 0.0;
  double placement_prepare_work_seconds = 0.0;
  double db_insert_seconds = 0.0;
  double post_fill_density_seconds = 0.0;
  int placement_threads = 1;
  double placement_select_seconds = 0.0;
  double placement_select_work_seconds = 0.0;
  std::map<std::string, double> placement_phase_seconds{};
  std::map<std::string, double> candidate_phase_seconds{};
  std::string lp_solver = "unknown";
  int64_t solver_iterations = -1;
  double min_post_fill_tile_density = 0.0;
  double max_post_fill_tile_density = 0.0;
  size_t min_violation_tile_count = 0;
  size_t max_violation_tile_count = 0;
};

void writeFloatingDensityProfile(const std::string& profile_filename,
                                 const char* layer_name,
                                 const std::string& algorithm,
                                 const FloatingDensityResult& floating_density,
                                 double min_window_density,
                                 double max_density,
                                 utl::Logger* logger);
void writeTileDensityReport(
    const char* density_report_filename,
    const std::vector<TileDensityReport>& density_reports,
    double compute_seconds,
    double svg_seconds,
    double min_window_density,
    double max_density,
    utl::Logger* logger);
void writeLPFillDensityReport(
    const char* density_report_filename,
    const std::vector<LPFillDensityReport>& density_reports,
    double compute_seconds,
    double svg_seconds,
    double min_window_density,
    double max_window_density,
    utl::Logger* logger);

}  // namespace fin

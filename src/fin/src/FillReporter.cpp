// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillReporter.h"

#include <fstream>
#include <iomanip>

#include "LpDiagnostics.h"
#include "utl/Logger.h"

namespace fin {
using utl::FIN;
namespace {
void writeWindowDensityHistogram(std::ostream& report,
                                 const WindowDensityHistogram& histogram)
{
  report << ", \"window_density_histogram\": {\"bin_width\": "
         << 1.0 / WindowDensityHistogramBins << ", \"counts\": [";
  for (size_t bin = 0; bin < histogram.size(); bin++) {
    report << histogram[bin] << (bin + 1 == histogram.size() ? "]}" : ", ");
  }
}
}  // namespace

void writeFloatingDensityProfile(const std::string& profile_filename,
                                 const char* layer_name,
                                 const std::string& algorithm,
                                 const FloatingDensityResult& floating_density,
                                 double min_window_density,
                                 double max_density,
                                 utl::Logger* logger)
{
  std::ofstream profile(profile_filename);
  if (!profile) {
    logger->warn(FIN,
                 65,
                 "Cannot write floating-density profile to {}.",
                 profile_filename);
  } else {
    profile << std::fixed << std::setprecision(9);
    profile << "{\n  \"layer\": \"" << layer_name << "\",\n  \"algorithm\": \""
            << algorithm
            << "\",\n  \"min_density\": " << floating_density.min_density
            << ",\n  \"max_density\": " << floating_density.max_density
            << ",\n  \"min_density_limit\": " << min_window_density;
    if (max_density >= 0.0) {
      profile << ",\n  \"max_density_limit\": " << max_density;
    }
    profile << ",\n  \"profiles\": [\n";
    for (size_t profile_index = 0;
         profile_index < floating_density.profiles.size();
         profile_index++) {
      const FloatingDensityResult::Profile& entry
          = floating_density.profiles[profile_index];
      profile << "    {\"label\": \"window y=" << entry.y
              << "\", \"y\": " << entry.y << ", \"points\": [";
      for (size_t point_index = 0; point_index < entry.points.size();
           point_index++) {
        const auto& point = entry.points[point_index];
        profile << "[" << point.first << ", " << point.second << "]"
                << (point_index + 1 == entry.points.size() ? "" : ", ");
      }
      profile << "]}"
              << (profile_index + 1 == floating_density.profiles.size()
                      ? "\n"
                      : ",\n");
    }
    profile << "  ]\n}\n";
    logger->info(FIN,
                 66,
                 "Floating density: layer={}, min={:.6f}, max={:.6f}, "
                 "profile={}",
                 layer_name,
                 floating_density.min_density,
                 floating_density.max_density,
                 profile_filename);
  }
}

void writeTileDensityReport(
    const char* density_report_filename,
    const std::vector<TileDensityReport>& density_reports,
    double compute_seconds,
    double svg_seconds,
    double min_window_density,
    double max_density,
    utl::Logger* logger)
{
  if (density_report_filename != nullptr
      && density_report_filename[0] != '\0') {
    std::ofstream report(density_report_filename);
    if (!report) {
      logger->warn(FIN,
                   59,
                   "Cannot write tile-grid density report to {}.",
                   density_report_filename);
    } else {
      report << std::fixed << std::setprecision(6);
      report << "{\n  \"runtime_seconds\": {\n"
             << "    \"density_analysis_excluding_svg\": " << compute_seconds
             << ",\n"
             << "    \"svg\": " << svg_seconds << "\n  },\n"
             << "  \"min_window_density_limit\": " << min_window_density
             << ",\n"
             << "  \"max_density_limit\": " << max_density << ",\n"
             << "  \"layers\": [\n";
      for (size_t index = 0; index < density_reports.size(); index++) {
        const TileDensityReport& entry = density_reports[index];
        report << "    {\"layer\": \"" << entry.name
               << "\", \"window_count\": " << entry.window_count
               << ", \"min_density\": " << entry.min_density
               << ", \"max_density\": " << entry.max_density
               << ", \"mean_density\": " << entry.mean_density
               << ", \"variance\": " << entry.variance
               << ", \"min_violation_window_count\": "
               << entry.min_violation_window_count
               << ", \"max_violation_window_count\": "
               << entry.max_violation_window_count
               << ", \"runtime_seconds\": {\"compute\": "
               << entry.compute_seconds << ", \"svg\": " << entry.svg_seconds
               << "}";
        writeWindowDensityHistogram(report, entry.histogram);
        report << "}" << (index + 1 == density_reports.size() ? "\n" : ",\n");
      }
      report << "  ]\n}\n";
    }
  }
}

void writeLPFillDensityReport(
    const char* density_report_filename,
    const std::vector<LPFillDensityReport>& density_reports,
    double compute_seconds,
    double svg_seconds,
    double min_window_density,
    double max_window_density,
    utl::Logger* logger)
{
  if (density_report_filename != nullptr
      && density_report_filename[0] != '\0') {
    std::ofstream report(density_report_filename);
    if (!report) {
      logger->warn(FIN,
                   57,
                   "Cannot write fixed-dissection LP density report to {}.",
                   density_report_filename);
    } else {
      report << std::fixed << std::setprecision(6);
      report << "{\n  \"runtime_seconds\": {\n"
             << "    \"compute_excluding_svg\": " << compute_seconds << ",\n"
             << "    \"svg\": " << svg_seconds << "\n  },\n"
             << "  \"min_window_density_limit\": " << min_window_density
             << ",\n"
             << "  \"max_window_density_limit\": " << max_window_density
             << ",\n"
             << "  \"layers\": [\n";
      for (size_t index = 0; index < density_reports.size(); index++) {
        const LPFillDensityReport& entry = density_reports[index];
        report << "    {\"layer\": \"" << entry.name
               << "\", \"planned_fill_area\": " << entry.planned_fill_area
               << ", \"placed_fill_area\": " << entry.placed_fill_area
               << ", \"window_count\": " << entry.window_count
               << ", \"min_density\": " << entry.min_density
               << ", \"max_density\": " << entry.max_density
               << ", \"mean_density\": " << entry.mean_density
               << ", \"variance\": " << entry.variance
               << ", \"min_violation_window_count\": "
               << entry.min_violation_window_count
               << ", \"max_violation_window_count\": "
               << entry.max_violation_window_count;
        writeWindowDensityHistogram(report, entry.histogram);
        if (entry.solved) {
          report << ", \"min_post_fill_tile_density\": "
                 << entry.min_post_fill_tile_density
                 << ", \"max_post_fill_tile_density\": "
                 << entry.max_post_fill_tile_density
                 << ", \"min_violation_tile_count\": "
                 << entry.min_violation_tile_count
                 << ", \"max_violation_tile_count\": "
                 << entry.max_violation_tile_count;
        }
        report << ", \"lp_solver\": " << lpJsonString(entry.lp_solver)
               << ", \"solver_iterations\": ";
        if (entry.solver_iterations >= 0) {
          report << entry.solver_iterations;
        } else {
          report << "null";
        }
        report << ", \"solved\": " << (entry.solved ? "true" : "false")
               << ", \"status\": \""
               << (entry.solved ? "solved"
                                : (entry.solver_status_code == 2
                                           || entry.solver_status == "NOT_RUN"
                                       ? "infeasible"
                                       : "failed"))
               << "\""
               << ", \"tile_violation_count\": " << entry.tile_violation_count
               << ", \"window_violation_count\": "
               << entry.window_violation_count << ", \"failure\": ";
        if (entry.solved) {
          report << "null";
        } else {
          const bool has_location
              = entry.infeasibility_reason
                    == "window_lower_bound_exceeds_upper_bound"
                || entry.infeasibility_reason
                       == "window_lower_bound_exceeds_legal_capacity";
          report << "{\"reason\": \"" << entry.infeasibility_reason
                 << "\", \"solver_status\": "
                 << lpJsonString(entry.solver_status)
                 << ", \"solver_status_code\": " << entry.solver_status_code
                 << ", \"diagnostics_file\": "
                 << lpJsonString(entry.diagnostics_prefix + ".json")
                 << ", \"window_index\": ";
          if (has_location) {
            report << entry.diagnostic_window_index << ", \"bounds\": ["
                   << entry.diagnostic_window_bounds.xMin() << ", "
                   << entry.diagnostic_window_bounds.yMin() << ", "
                   << entry.diagnostic_window_bounds.xMax() << ", "
                   << entry.diagnostic_window_bounds.yMax()
                   << "], \"current_density\": "
                   << entry.diagnostic_current_density
                   << ", \"required_fill_area\": "
                   << entry.diagnostic_required_fill_area
                   << ", \"max_legal_fill_area\": "
                   << entry.diagnostic_max_legal_fill_area
                   << ", \"max_window_fill_budget\": "
                   << entry.diagnostic_fill_budget;
          } else {
            report << "null, \"bounds\": null";
          }
          report << "}";
        }
        report << ", \"runtime_seconds\": {\"candidates\": "
               << entry.candidate_seconds << ", \"candidate_generation\": "
               << entry.candidate_generation_seconds
               << ", \"candidate_generation_work\": "
               << entry.candidate_generation_work_seconds
               << ", \"candidate_threads\": " << entry.candidate_threads
               << ", \"candidate_prepare_threads\": "
               << entry.candidate_prepare_threads
               << ", \"lp_solve\": " << entry.solve_seconds
               << ", \"placement\": " << entry.placement_seconds
               << ", \"placement_select\": " << entry.placement_select_seconds
               << ", \"placement_select_work\": "
               << entry.placement_select_work_seconds
               << ", \"placement_prepare\": " << entry.placement_prepare_seconds
               << ", \"placement_prepare_work\": "
               << entry.placement_prepare_work_seconds
               << ", \"db_insert\": " << entry.db_insert_seconds
               << ", \"post_fill_density\": " << entry.post_fill_density_seconds
               << ", \"placement_threads\": " << entry.placement_threads
               << ", \"candidate_phases\": {";
        bool first_candidate_phase = true;
        for (const auto& [name, value] : entry.candidate_phase_seconds) {
          report << (first_candidate_phase ? "" : ", ") << lpJsonString(name)
                 << ": " << value;
          first_candidate_phase = false;
        }
        report << "}, \"placement_phases\": {";
        bool first_phase = true;
        for (const auto& [name, value] : entry.placement_phase_seconds) {
          report << (first_phase ? "" : ", ") << lpJsonString(name) << ": "
                 << value;
          first_phase = false;
        }
        report << "}"
               << ", \"compute\": " << entry.compute_seconds
               << ", \"diagnostics\": " << entry.diagnostics_seconds
               << ", \"svg\": " << entry.svg_seconds << "}}"
               << (index + 1 == density_reports.size() ? "\n" : ",\n");
      }
      report << "  ]\n}\n";
    }
  }
}
}  // namespace fin

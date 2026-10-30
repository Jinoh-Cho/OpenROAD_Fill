// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "FillReporter.h"
#include "FixedDissectionLp.h"
#include "LipLpFill.h"
#include "LpDiagnostics.h"
#include "MinFillAmountLp.h"
#include "MinVarLP.h"
#include "TileGrid.h"
#include "gtest/gtest.h"
#include "ortools/math_opt/io/lp_parser.h"

namespace fin {
namespace {

class UnattainableTileMinimumTest : public testing::TestWithParam<int>
{
 protected:
  FixedDissectionLpResult solve(const FixedDissectionLpProblem& problem)
  {
    if (GetParam() == 0) {
      return solveMinVarLP(problem);
    }
    if (GetParam() == 1) {
      return solveMinFillAmountLp(problem);
    }
    return solveLipLpFill(problem, {{0, 1}});
  }

  FixedDissectionLpProblem problem_{
      .max_window_density = 0.6,
      .min_tile_density = 0.2,
      .tile_areas = {10.0, 10.0, 10.0},
      .feature_areas = {0.0, 0.0, 0.0},
      .max_fill_areas = {0.0, 10.0, 1.0},
      .windows = {{0, 1}, {1, 2}},
  };
};

TEST_P(UnattainableTileMinimumTest, OmitsOnlyUnattainableTileMinimums)
{
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 3);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-6);
  EXPECT_GE(result.fill_areas[1], 2.0 - 1e-6);
  EXPECT_LE(result.fill_areas[2], 1.0 + 1e-6);
  for (const auto& window : problem_.windows) {
    EXPECT_LE(result.fill_areas[window[0]] + result.fill_areas[window[1]],
              12.0 + 1e-6);
  }
}

TEST_P(UnattainableTileMinimumTest, RetainsWindowMinimums)
{
  problem_.min_window_density = 0.15;
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_GE(result.fill_areas[1], 3.0 - 1e-6);

  problem_.max_fill_areas[1] = 1.0;
  EXPECT_FALSE(solve(problem_).solved);
}

TEST_P(UnattainableTileMinimumTest, RetainsMaximumTileDensity)
{
  problem_.min_tile_density = 0.05;
  problem_.max_tile_density = 0.1;
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_LE(result.fill_areas[1], 1.0 + 1e-6);
  EXPECT_LE(result.fill_areas[2], 1.0 + 1e-6);

  problem_.min_window_density = 0.1;
  EXPECT_FALSE(solve(problem_).solved);
}

TEST_P(UnattainableTileMinimumTest, OmitsTileMaximumExceededByExistingMetal)
{
  problem_.max_tile_density = 0.3;
  problem_.feature_areas = {4.0, 0.0, 0.0};
  problem_.max_fill_areas = {2.0, 10.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_LE(result.fill_areas[0], 2.0 + 1e-6);
  EXPECT_LE(result.fill_areas[1], 3.0 + 1e-6);
  EXPECT_LE(result.fill_areas[2], 3.0 + 1e-6);
  const auto min_var = solveMinVarLP(problem_);
  ASSERT_TRUE(min_var.solved);
  EXPECT_NEAR(min_var.fill_areas[0], 2.0, 1e-6);
}

TEST_P(UnattainableTileMinimumTest, OmitsWindowMaximumExceededByExistingMetal)
{
  problem_.max_window_density = 0.3;
  problem_.feature_areas = {7.0, 0.0, 0.0};
  problem_.max_fill_areas = {0.0, 10.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_GE(result.fill_areas[1], 2.0 - 1e-6);
  EXPECT_GE(result.fill_areas[2], 2.0 - 1e-6);
  EXPECT_LE(result.fill_areas[1] + result.fill_areas[2], 6.0 + 1e-6);
}

TEST_P(UnattainableTileMinimumTest, RetainsTileMaximumAtExistingLimit)
{
  problem_.max_tile_density = 0.3;
  problem_.feature_areas = {3.0, 0.0, 0.0};
  problem_.max_fill_areas = {7.0, 10.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-6);
}

TEST_P(UnattainableTileMinimumTest, RetainsWindowMaximumAtExistingLimit)
{
  problem_.min_tile_density = 0.0;
  problem_.feature_areas = {6.0, 6.0, 0.0};
  problem_.max_fill_areas = {4.0, 4.0, 10.0};
  const auto result = solve(problem_);

  ASSERT_TRUE(result.solved);
  EXPECT_NEAR(result.fill_areas[0], 0.0, 1e-6);
  EXPECT_NEAR(result.fill_areas[1], 0.0, 1e-6);
}

INSTANTIATE_TEST_SUITE_P(AllPlanners,
                         UnattainableTileMinimumTest,
                         testing::Values(0, 1, 2));

class PdlpFillTest : public testing::TestWithParam<std::tuple<int, int>>
{
 protected:
  FixedDissectionLpResult solve(const FixedDissectionLpProblem& problem)
  {
    if (std::get<0>(GetParam()) == 0) {
      return solveMinVarLP(problem);
    }
    if (std::get<0>(GetParam()) == 1) {
      return solveMinFillAmountLp(problem);
    }
    return solveLipLpFill(problem, {{0, 1}});
  }
};

TEST_P(PdlpFillTest, SolvesWithSelectedBackend)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.8,
      .min_window_density = 0.3,
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 8.0},
      .max_fill_areas = {10.0, 2.0},
      .windows = {{0}, {1}},
      .lp_solver = FillLpSolver::Pdlp,
      .lp_threads = std::get<1>(GetParam()),
  };
  const auto result = solve(problem);
  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_GE(result.fill_areas[0], 3.0 - 1e-4);
  EXPECT_LE(result.fill_areas[0], 8.0 + 1e-4);
  EXPECT_NEAR(result.fill_areas[1], 0.0, 1e-4);
  if (std::get<0>(GetParam()) == 0) {
    EXPECT_NEAR(result.min_window_area, 8.0, 1e-4);
  } else if (std::get<0>(GetParam()) == 1) {
    EXPECT_NEAR(result.fill_areas[0], 3.0, 1e-4);
  } else {
    EXPECT_NEAR(result.lip_value, 0.0, 1e-4);
    EXPECT_NEAR(result.fill_areas[0], 8.0, 1e-4);
  }

  for (const double tolerance : {1e-6, 1e-5}) {
    problem.dual_tolerance = tolerance;
    const auto configured = solve(problem);
    ASSERT_TRUE(configured.solved);
    ASSERT_EQ(configured.fill_areas.size(), result.fill_areas.size());
    for (size_t index = 0; index < result.fill_areas.size(); ++index) {
      EXPECT_NEAR(configured.fill_areas[index], result.fill_areas[index], 1e-4);
    }
  }
  problem.min_window_density = 0.9;
  EXPECT_FALSE(solve(problem).solved);
}

TEST_P(PdlpFillTest, RejectsInvalidThreadCount)
{
  FixedDissectionLpProblem problem{
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 0.0},
      .max_fill_areas = {10.0, 10.0},
      .windows = {{0}, {1}},
      .lp_solver = FillLpSolver::Pdlp,
      .lp_threads = 0,
  };
  EXPECT_THROW(solve(problem), std::invalid_argument);
  problem.lp_solver = FillLpSolver::Glop;
  problem.lp_threads = 2;
  EXPECT_THROW(solve(problem), std::invalid_argument);
}

TEST_P(PdlpFillTest, RejectsInvalidDualTolerance)
{
  FixedDissectionLpProblem problem{
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 0.0},
      .max_fill_areas = {10.0, 10.0},
      .windows = {{0}, {1}},
      .lp_solver = FillLpSolver::Pdlp,
      .lp_threads = std::get<1>(GetParam()),
  };
  for (const double value : {-1.0,
                             std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    problem.dual_tolerance = value;
    EXPECT_THROW(solve(problem), std::invalid_argument);
  }
  problem.lp_solver = FillLpSolver::Glop;
  problem.lp_threads = 1;
  problem.dual_tolerance = 1e-5;
  EXPECT_THROW(solve(problem), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(AllPlanners,
                         PdlpFillTest,
                         testing::Combine(testing::Values(0, 1, 2),
                                          testing::Values(1, 2, 4)));

class LpAreaScalingTest
    : public testing::TestWithParam<std::tuple<FillLpSolver, int, double>>
{
};

TEST_P(LpAreaScalingTest, PreservesDensityAndReturnsAreasInInputUnits)
{
  const auto [solver, objective, unit_scale] = GetParam();
  // Unequal tiles and existing metal exercise both window bounds and the
  // max-min objective's feature-area offset. The solution is unique.
  FixedDissectionLpProblem problem{
      .max_window_density = 0.8,
      .min_window_density = 0.3,
      .tile_areas = {10.0 * unit_scale, 20.0 * unit_scale},
      .feature_areas = {0.0, 16.0 * unit_scale},
      .max_fill_areas = {10.0 * unit_scale, 4.0 * unit_scale},
      .windows = {{0}, {1}},
      .lp_solver = solver,
  };
  const auto solve = [&]() {
    return objective == 0 ? solveMinVarLP(problem)
                          : solveMinFillAmountLp(problem);
  };
  const auto result = solve();
  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(
      result.fill_areas[0] / unit_scale, objective == 0 ? 8.0 : 3.0, 1e-4);
  EXPECT_NEAR(result.fill_areas[1] / unit_scale, 0.0, 1e-4);
  EXPECT_NEAR(
      result.min_window_area / unit_scale, objective == 0 ? 8.0 : 3.0, 1e-4);
  // Changing units must not hide a genuinely infeasible window.
  problem.min_window_density = 0.9;
  EXPECT_FALSE(solve().solved);
}

INSTANTIATE_TEST_SUITE_P(
    BothBackends,
    LpAreaScalingTest,
    testing::Combine(testing::Values(FillLpSolver::Glop, FillLpSolver::Pdlp),
                     testing::Values(0, 1),
                     testing::Values(1e-6, 1.0, 15625000.0)));

class LpFailureDiagnosticsTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    const auto stamp
        = std::chrono::steady_clock::now().time_since_epoch().count();
    directory_ = std::filesystem::temp_directory_path()
                 / ("fin_lp_diagnostics_" + std::to_string(stamp));
    ASSERT_TRUE(std::filesystem::create_directory(directory_));
  }
  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path directory_;
};

TEST_F(LpFailureDiagnosticsTest, RecordsConflictAcrossOverlappingWindows)
{
  const TileGrid grid({odb::Rect(0, 0, 30, 20), odb::Point(0, 0), 20, 2});
  FixedDissectionLpProblem problem{
      .max_window_density = 0.03,
      .min_window_density = 0.025,
      .tile_areas = {100, 100, 100, 100, 100, 100},
      .feature_areas = {0, 0, 8, 0, 0, 0},
      .max_fill_areas = {1, 10, 10, 0, 0, 0},
      .windows = grid.windowTileIndices(),
      .diagnostics_prefix = (directory_ / "failure").string(),
  };
  const auto result = solveMinFillAmountLp(problem);
  ASSERT_FALSE(result.solved);
  EXPECT_EQ(result.solver_status, "INFEASIBLE");
  ASSERT_TRUE(result.model_exported);
  writeFillLpFailureDiagnostics(problem, grid, "met2", result, 0.025, "");
  boost::property_tree::ptree report;
  boost::property_tree::read_json(problem.diagnostics_prefix + ".json", report);
  EXPECT_TRUE(report.get<bool>("diagnosis.conflict_identified"));
  EXPECT_DOUBLE_EQ(report.get<double>("model_area_scale"), 100.0);
  EXPECT_FALSE(report.get<bool>("diagnosis.is_iis"));
  EXPECT_GT(report.get<int>("diagnosis.update_count"), 0);
  EXPECT_EQ(report.get<int>("conflict.window_index"), 1);
  EXPECT_EQ(report.get<std::string>("conflict.reason"),
            "tile_lower_sum_exceeds_window_upper_bound");
  EXPECT_GT(report.get<double>("conflict.propagated_tile_lower_sum_dbu2"),
            report.get<double>("conflict.upper_fill_dbu2"));
  for (const char* extension : {".lp",
                                ".constraints.csv",
                                ".tiles.csv",
                                ".windows.csv",
                                ".bound_updates.csv"}) {
    EXPECT_GT(
        std::filesystem::file_size(problem.diagnostics_prefix + extension), 0);
  }
  const auto verify_model
      = [](const std::string& prefix, int variables, int constraints) {
          std::ifstream stream(prefix + ".lp");
          const std::string text((std::istreambuf_iterator<char>(stream)), {});
          const auto parsed
              = operations_research::math_opt::ModelProtoFromLp(text);
          ASSERT_TRUE(parsed.ok()) << parsed.status();
          EXPECT_EQ(parsed->variables().ids_size(), variables);
          EXPECT_EQ(parsed->linear_constraints().ids_size(), constraints);
        };
  verify_model(problem.diagnostics_prefix, 6, 4);
  problem.diagnostics_prefix = (directory_ / "minvar").string();
  EXPECT_FALSE(solveMinVarLP(problem).solved);
  verify_model(problem.diagnostics_prefix, 7, 6);
  problem.diagnostics_prefix = (directory_ / "lip").string();
  EXPECT_FALSE(solveLipLpFill(problem, {{0, 1}}).solved);
  verify_model(problem.diagnostics_prefix, 9, 9);
}

TEST_F(LpFailureDiagnosticsTest, RecordsContradictoryTileBounds)
{
  const TileGrid grid({odb::Rect(0, 0, 10, 10), odb::Point(0, 0), 10, 1});
  FixedDissectionLpProblem problem{
      .min_tile_density = 0.8,
      .max_tile_density = 0.2,
      .tile_areas = {100},
      .feature_areas = {0},
      .max_fill_areas = {100},
      .windows = grid.windowTileIndices(),
      .diagnostics_prefix = (directory_ / "tile_conflict").string(),
  };
  const auto failure = solveMinFillAmountLp(problem);
  ASSERT_FALSE(failure.solved);
  writeFillLpFailureDiagnostics(problem, grid, "met2", failure, 0.0, "");
  boost::property_tree::ptree report;
  boost::property_tree::read_json(problem.diagnostics_prefix + ".json", report);
  EXPECT_TRUE(report.get<bool>("diagnosis.conflict_identified"));
  EXPECT_EQ(report.get<int>("conflict.tile_index"), 0);
  EXPECT_EQ(report.get<std::string>("conflict.window_index"), "null");
  EXPECT_GT(report.get<double>("conflict.tile_lower_dbu2"),
            report.get<double>("conflict.tile_upper_dbu2"));
}

TEST_F(LpFailureDiagnosticsTest, DoesNotInventConflictingLocation)
{
  const TileGrid grid({odb::Rect(0, 0, 10, 10), odb::Point(0, 0), 10, 1});
  FixedDissectionLpProblem problem{
      .tile_areas = {100},
      .feature_areas = {0},
      .max_fill_areas = {100},
      .windows = grid.windowTileIndices(),
      .diagnostics_prefix = (directory_ / "unresolved").string(),
  };
  FixedDissectionLpResult failure;
  failure.solver_status = "ABNORMAL";
  failure.solver_status_code = 4;
  writeFillLpFailureDiagnostics(problem, grid, "met\"2", failure, 0.0, "");
  boost::property_tree::ptree report;
  boost::property_tree::read_json(problem.diagnostics_prefix + ".json", report);
  EXPECT_EQ(report.get<std::string>("layer"), "met\"2");
  EXPECT_FALSE(report.get<bool>("diagnosis.conflict_identified"));
  EXPECT_EQ(report.get<std::string>("conflict"), "null");
  EXPECT_EQ(report.get<std::string>("model_file"), "null");

  LPFillDensityReport entry{};
  entry.name = "met2";
  entry.infeasibility_reason = "lp_solver_failure";
  entry.solver_status = "ABNORMAL";
  entry.solver_status_code = 4;
  entry.diagnostics_prefix = problem.diagnostics_prefix;
  const auto path = (directory_ / "density.json").string();
  writeLPFillDensityReport(path.c_str(), {entry}, 0, 0, 0, 1, nullptr);
  boost::property_tree::read_json(path, report);
  const auto& layer = report.get_child("layers").front().second;
  EXPECT_EQ(layer.get<std::string>("status"), "failed");
  EXPECT_EQ(layer.get<std::string>("failure.window_index"), "null");
  EXPECT_EQ(layer.get<std::string>("failure.bounds"), "null");
}

TEST(FixedDissectionLpTest, EnforcesMinimumWindowDensity)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.5,
      .min_window_density = 0.4,
      .tile_areas = {10.0},
      .feature_areas = {0.0},
      .max_fill_areas = {3.0},
      .windows = {{0}},
  };

  EXPECT_FALSE(solveMinVarLP(problem).solved);
}

TEST(FixedDissectionLpTest, RejectsInvalidMinimumWindowDensity)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.5,
      .min_window_density = 1.1,
      .tile_areas = {10.0},
      .feature_areas = {0.0},
      .max_fill_areas = {10.0},
      .windows = {{0}},
  };

  EXPECT_THROW(solveMinVarLP(problem), std::invalid_argument);
}

TEST(FixedDissectionLpTest, EnforcesMaximumTileDensity)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.9,
      .max_tile_density = 0.3,
      .tile_areas = {10.0},
      .feature_areas = {0.0},
      .max_fill_areas = {10.0},
      .windows = {{0}},
  };

  const FixedDissectionLpResult result = solveMinVarLP(problem);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 1);
  EXPECT_NEAR(result.fill_areas[0], 3.0, 1e-6);
}

TEST(FixedDissectionLpTest, MinFillAmountUsesNamedPlanner)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.8,
      .min_window_density = 0.3,
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 0.0},
      .max_fill_areas = {10.0, 10.0},
      .windows = {{0}, {1}},
  };

  const FixedDissectionLpResult result = solveMinFillAmountLp(problem);

  ASSERT_TRUE(result.solved);
  ASSERT_EQ(result.fill_areas.size(), 2);
  EXPECT_NEAR(result.fill_areas[0], 3.0, 1e-6);
  EXPECT_NEAR(result.fill_areas[1], 3.0, 1e-6);

  const FixedDissectionLpResult min_var = solveMinVarLP(problem);
  ASSERT_TRUE(min_var.solved);
  EXPECT_NEAR(min_var.min_window_area, 8.0, 1e-6);
  EXPECT_NEAR(min_var.fill_areas[0], 8.0, 1e-6);
  EXPECT_NEAR(min_var.fill_areas[1], 8.0, 1e-6);
}

TEST(FixedDissectionLpTest, MinimizesLipDensityRange)
{
  FixedDissectionLpProblem problem{
      .max_window_density = 0.8,
      .min_tile_density = 0.0,
      .max_tile_density = 1.0,
      .min_window_density = 0.3,
      .tile_areas = {10.0, 10.0},
      .feature_areas = {0.0, 8.0},
      .max_fill_areas = {10.0, 2.0},
      .windows = {{0}, {1}},
  };

  const FixedDissectionLpResult result = solveLipLpFill(problem, {{0, 1}});

  ASSERT_TRUE(result.solved);
  EXPECT_NEAR(result.lip_value, 0.0, 1e-6);
  const double first_density = result.fill_areas[0] / 10.0;
  const double second_density = (8.0 + result.fill_areas[1]) / 10.0;
  EXPECT_NEAR(first_density, second_density, 1e-6);
}

}  // namespace
}  // namespace fin

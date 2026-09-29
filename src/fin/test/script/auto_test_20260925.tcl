#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
run_date="$(date +%Y%m%d)"

run_test() {
  local benchmark="$1"
  local test_name="$2"
  local density_report_name="${3:-}"
  local output_dir="$test_dir/results/$run_date/$benchmark"
  mkdir -p "$output_dir"
  "$repo_dir/bazel-bin/openroad" "$script_dir/$test_name.tcl" 2>&1 \
    | tee "$output_dir/$test_name.log"
  if [[ -n "$density_report_name" ]]; then
    python3 "$script_dir/plot_window_density_histogram.py" \
      "$output_dir/$density_report_name.json" \
      "$output_dir/${density_report_name}_histogram.svg"
  fi
}

run_test gcd gcd_density_fill_svg gcd_density_fill_density
run_test gcd gcd_fixed_dissection_lp_fill gcd_fixed_dissection_lp_fill_density

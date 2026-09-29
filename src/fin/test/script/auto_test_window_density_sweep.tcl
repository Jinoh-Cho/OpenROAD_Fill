#!/usr/bin/env bash
set -euo pipefail

if (( $# == 0 )); then
  echo "usage: $0 [--rules FILE] MIN:MAX [MIN:MAX ...]" >&2
  echo "example: $0 --rules src/fin/test/fill_small_pattern.json 0.35:0.45" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
run_date="$(date +%Y%m%d)"
rules_file="$test_dir/fill.json"
if [[ "${1:-}" == "--rules" ]]; then
  if (( $# < 3 )); then
    echo "--rules requires a file followed by at least one MIN:MAX range" >&2
    exit 2
  fi
  rules_file="$2"
  shift 2
fi
if [[ ! -f "$rules_file" ]]; then
  echo "fill rules file does not exist: $rules_file" >&2
  exit 2
fi
rules_tag="$(basename "$rules_file" .json)"

run_experiment() {
  local range="$1"
  local minimum="${range%%:*}"
  local maximum="${range#*:}"
  if [[ "$minimum" == "$range" || -z "$minimum" || -z "$maximum" || "$maximum" == *:* ]]; then
    echo "invalid density range '$range'; use MIN:MAX (for example, 0.30:0.45)" >&2
    exit 2
  fi
  local min_tag="${minimum/./_}"
  local max_tag="${maximum/./_}"
  local result_name="gcd_fixed_dissection_lp_fill_min_${min_tag}_max_${max_tag}"
  local output_dir="$test_dir/results/$run_date/gcd/window_density_sweep/$rules_tag/min_$min_tag/max_$max_tag"
  mkdir -p "$output_dir"
  FIN_FILL_RULES="$rules_file" FIN_MIN_WINDOW_DENSITY="$minimum" FIN_MAX_WINDOW_DENSITY="$maximum" \
    "$repo_dir/bazel-bin/openroad" "$script_dir/gcd_fixed_dissection_lp_fill_sweep.tcl" 2>&1 \
    | tee "$output_dir/$result_name.log"
  python3 "$script_dir/plot_window_density_histogram.py" \
    "$output_dir/${result_name}_density.json" \
    "$output_dir/${result_name}_density_histogram.svg"
}

for range in "$@"; do
  run_experiment "$range"
done

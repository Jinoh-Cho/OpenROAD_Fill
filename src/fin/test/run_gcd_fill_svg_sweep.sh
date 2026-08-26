#!/usr/bin/env bash
# Generate density-fill SVGs for several non-OPC spacing values.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/../../.." && pwd)
openroad_bin=${OPENROAD_BIN:-"$repo_root/bazel-bin/openroad"}
output_root=${1:-"$script_dir/results/fill_svg_space_sweep"}

for spacing in 0.5 1 1.5 2 2.5 3; do
  case_dir="$output_root/space_to_non_fill_${spacing}um"
  rules_file="$case_dir/fill.json"
  mkdir -p "$case_dir"
  jq --argjson spacing "$spacing" \
    '(.layers[] | .["non-opc"].space_to_non_fill) = $spacing' \
    "$script_dir/fill.json" > "$rules_file"
  FIN_FILL_RULES="$rules_file" FIN_FILL_SVG_DIR="$case_dir" \
    "$openroad_bin" "$script_dir/gcd_fill_svg.tcl"
done

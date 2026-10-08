#!/usr/bin/env bash
# Smoke-test the floating-density SVG profile renderer.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
output_dir="$(mktemp -d)"
trap 'rm -rf -- "$output_dir"' EXIT

output_svg="$output_dir/floating_density_profile.svg"
python3 "$script_dir/plot_floating_density_profile.py" \
  "$script_dir/floating_density_profile_example.json" \
  "$output_svg"

test -s "$output_svg"
grep -q '<polyline' "$output_svg"
grep -q 'minimum density limit' "$output_svg"
grep -q 'maximum density limit' "$output_svg"

echo "Floating-density profile SVG smoke test passed: $output_svg"

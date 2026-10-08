#!/usr/bin/env bash
# Run exact ALG2 density extraction and render one SVG profile per GCD layer.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
openroad_bin="${OPENROAD_BIN:-$repo_dir/bazel-bin/openroad}"
run_date="$(date +%Y%m%d)"
output_dir="$test_dir/results/$run_date/gcd/floating_density"

if [[ ! -x "$openroad_bin" ]]; then
  echo "OpenROAD executable not found: $openroad_bin" >&2
  echo "Build it first, or set OPENROAD_BIN to its path." >&2
  exit 1
fi

"$openroad_bin" "$script_dir/gcd_floating_density_profile.tcl"

shopt -s nullglob
profiles=("$output_dir"/gcd_floating_density_*.json)
if (( ${#profiles[@]} == 0 )); then
  echo "No floating-density JSON profiles were generated in $output_dir" >&2
  exit 1
fi

for profile in "${profiles[@]}"; do
  python3 "$script_dir/plot_floating_density_profile.py" \
    "$profile" "${profile%.json}.svg"
done

echo "Floating-density JSON and SVG profiles: $output_dir"

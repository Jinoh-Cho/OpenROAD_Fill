#!/usr/bin/env bash
# Run max-min fixed-dissection LP fill at multiple resolutions and render
# exact ALG2 floating-density profiles for every metal layer.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
openroad_bin="${OPENROAD_BIN:-$repo_dir/bazel-bin/openroad}"
run_date="$(date +%Y%m%d)"
results_group="${FIN_RESULTS_GROUP:-lp_resolution_sweep}"
results_root="$test_dir/results/$run_date/gcd/$results_group"
mkdir -p "$results_root"

if [[ ! -x "$openroad_bin" ]]; then
  echo "OpenROAD executable not found: $openroad_bin" >&2
  exit 1
fi

for resolution in 2 4 8 16; do
  min_tile_density="${FIN_MIN_TILE_DENSITY:-0.20}"
  min_tile_percent="${min_tile_density#0.}"
  tag="lp_minvar_min_tile_${min_tile_percent}_r${resolution}"
  if ! FIN_FILL_METHOD=lp_minvar \
      FIN_RESOLUTION="$resolution" \
      FIN_MIN_TILE_DENSITY="$min_tile_density" \
      FIN_RESULTS_GROUP="$results_group" \
      FIN_RESULTS_TAG="$tag" \
      "$openroad_bin" "$script_dir/gcd_fill_method_comparison.tcl" \
      | tee "$results_root/${tag}.log"; then
    echo "resolution $resolution: LP constraints are infeasible; skipping SVGs." >&2
    continue
  fi

  shopt -s nullglob
  profiles=("$results_root/$tag"/*_met*.json)
  if (( ${#profiles[@]} == 0 )); then
    echo "resolution $resolution: no density profiles were produced; skipping SVGs." >&2
    continue
  fi
  for profile in "${profiles[@]}"; do
    python3 "$script_dir/plot_floating_density_profile.py" \
      "$profile" "${profile%.json}.svg"
  done
done

python3 - "$results_root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
print("\nExact floating-density summary")
print("resolution  layer  min       max")
for directory in sorted(root.glob("lp_minvar_min_tile_*_r*")):
    resolution = directory.name.rsplit("_r", 1)[1]
    for profile in sorted(directory.glob("*_met*.json")):
        data = json.loads(profile.read_text())
        print(f"{resolution:10}  {data['layer']:5}  {data['min_density']:.6f} "
              f"{data['max_density']:.6f}")
PY

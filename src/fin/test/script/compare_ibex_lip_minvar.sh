#!/usr/bin/env bash
# Compare Lip1/Lip2/Lip3 and fixed-dissection LP min-var on IBEX.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
openroad_bin="${OPENROAD_BIN:-$repo_dir/bazel-bin/openroad}"
run_date="$(date +%Y%m%d)"
resolution="${FIN_RESOLUTION:-4}"
results_group="${FIN_RESULTS_GROUP:-lip_minvar_r${resolution}}"
rules_file="${FIN_RULES:-$test_dir/fill_tiny_pattern.json}"
min_tile_density="${FIN_MIN_TILE_DENSITY:-0.20}"
results_root="$test_dir/results/$run_date/ibex/$results_group"
mkdir -p "$results_root"

if [[ ! -x "$openroad_bin" ]]; then
  echo "OpenROAD executable not found: $openroad_bin" >&2
  exit 1
fi

for method in lip1 lip2 lip3 lp_minvar; do
  tag="${method}_r${resolution}"
  FIN_FILL_METHOD="$method" \
    FIN_RESOLUTION="$resolution" \
    FIN_MIN_TILE_DENSITY="$min_tile_density" \
    FIN_RULES="$rules_file" \
    FIN_RESULTS_GROUP="$results_group" \
    FIN_RESULTS_TAG="$tag" \
    "$openroad_bin" "$script_dir/ibex_lip_minvar_comparison.tcl" \
    | tee "$results_root/${tag}.log"
  if ! grep -q "FILL_RUNTIME: method=${method} resolution=${resolution}" \
      "$results_root/${tag}.log"; then
    echo "$method run did not reach the fill step; see $results_root/${tag}.log" >&2
    exit 1
  fi
done

python3 "$script_dir/plot_lip_lp_comparison.py" \
  "$results_root/lip1_r${resolution}/ibex_lip1_alg2_floating_density_met2.json" \
  "$results_root/lip2_r${resolution}/ibex_lip2_alg2_floating_density_met2.json" \
  "$results_root/lip3_r${resolution}/ibex_lip3_alg2_floating_density_met2.json" \
  "$results_root/lp_minvar_r${resolution}/ibex_lp_minvar_alg2_floating_density_met2.json" \
  "$results_root/met2_lip_minvar_comparison_r${resolution}.svg"

python3 - "$results_root" "$resolution" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
resolution = sys.argv[2]
print(f"\nIBEX Lip/min-var comparison (r={resolution}, MET2 continuous density)")
print("method    min_density  max_density  range      fill_area_DBU2  fill_runtime_s")
for method in ("lip1", "lip2", "lip3", "lp_minvar"):
    tag = f"{method}_r{resolution}"
    profile = root / tag / f"ibex_{method}_alg2_floating_density_met2.json"
    data = json.loads(profile.read_text())
    log = (root / f"{tag}.log").read_text()
    area = re.search(r"^placed_fill_area=([0-9.eE+-]+)", log, re.MULTILINE)
    runtime = re.search(r"FILL_RUNTIME: method=\S+ resolution=\S+ seconds=([0-9.]+)", log)
    low, high = data["min_density"], data["max_density"]
    print(f"{method:9} {low:.6f}     {high:.6f}     {high-low:.6f}  "
          f"{area.group(1) if area else 'n/a':>14}  {runtime.group(1) if runtime else 'n/a':>14}")
PY
echo "MET2_COMPARISON_SVG=$results_root/met2_lip_minvar_comparison_r${resolution}.svg"

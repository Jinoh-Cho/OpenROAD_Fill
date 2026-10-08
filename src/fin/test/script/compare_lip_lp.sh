#!/usr/bin/env bash
# Compare Lip1/Lip2/Lip3 LP fill from identical fresh GCD prefill databases.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
openroad_bin="${OPENROAD_BIN:-$repo_dir/bazel-bin/openroad}"
run_date="$(date +%Y%m%d)"
results_group="${FIN_RESULTS_GROUP:-lip_lp_comparison}"
rules_file="${FIN_RULES:-$test_dir/fill_tiny_pattern.json}"
resolution="${FIN_RESOLUTION:-2}"
min_tile_density="${FIN_MIN_TILE_DENSITY:-0.20}"
results_root="$test_dir/results/$run_date/gcd/$results_group"
mkdir -p "$results_root"

if [[ ! -x "$openroad_bin" ]]; then
  echo "OpenROAD executable not found: $openroad_bin" >&2
  exit 1
fi
if [[ ! -f "$rules_file" ]]; then
  echo "Fill rules file not found: $rules_file" >&2
  exit 1
fi

for lip_type in 1 2 3; do
  tag="lip${lip_type}_r${resolution}"
  FIN_LIP_TYPE="$lip_type" \
    FIN_RESOLUTION="$resolution" \
    FIN_MIN_TILE_DENSITY="$min_tile_density" \
    FIN_RULES="$rules_file" \
    FIN_RESULTS_GROUP="$results_group" \
    FIN_RESULTS_TAG="$tag" \
    "$openroad_bin" "$script_dir/gcd_lip_lp_comparison.tcl" \
    | tee "$results_root/${tag}.log"
  if ! grep -q "FILL_RUNTIME: lip=${lip_type} resolution=${resolution}" \
      "$results_root/${tag}.log"; then
    echo "Lip${lip_type} run did not reach the fill step; see $results_root/${tag}.log" >&2
    exit 1
  fi

  shopt -s nullglob
  profiles=("$results_root/$tag"/*_met*.json)
  for profile in "${profiles[@]}"; do
    python3 "$script_dir/plot_floating_density_profile.py" \
      "$profile" "${profile%.json}.svg" \
      --title "Lip${lip_type} floating-window density profile (r=${resolution})"
  done
done

met2_svg="$results_root/met2_lip_lp_comparison_r${resolution}.svg"
python3 "$script_dir/plot_lip_lp_comparison.py" \
  "$results_root/lip1_r${resolution}/gcd_lip1_alg2_floating_density_met2.json" \
  "$results_root/lip2_r${resolution}/gcd_lip2_alg2_floating_density_met2.json" \
  "$results_root/lip3_r${resolution}/gcd_lip3_alg2_floating_density_met2.json" \
  "$met2_svg"
echo "MET2_COMPARISON_SVG=$met2_svg"

python3 - "$results_root" "$resolution" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
resolution = sys.argv[2]
print(f"\nLip LP comparison (resolution r={resolution})")
print("Lip  alg   layer  L          min_density  max_density  total_fill_area_DBU2")
for lip in (1, 2, 3):
    tag = f"lip{lip}_r{resolution}"
    log_path = root / f"{tag}.log"
    log_text = log_path.read_text() if log_path.exists() else ""
    values = {}
    for match in re.finditer(
        rf"Lip{lip} LP objective L=([0-9.eE+-]+) on layer ([^\s.]+)", log_text
    ):
        values[match.group(2)] = match.group(1)
    area_match = re.search(r"^placed_fill_area=([0-9.eE+-]+)", log_text, re.MULTILINE)
    fill_area = area_match.group(1) if area_match else "n/a"
    for profile in sorted((root / tag).glob("*_met*.json")):
        data = json.loads(profile.read_text())
        layer = data.get("layer", profile.stem.split("_")[-1])
        algorithm = "alg2" if "_alg2_" in profile.name else "alg3"
        print(
            f"Lip{lip:<2} {algorithm:<5} {layer:<6} {values.get(layer, 'n/a'):<10} "
            f"{data['min_density']:.6f}     {data['max_density']:.6f}     {fill_area}"
        )
PY

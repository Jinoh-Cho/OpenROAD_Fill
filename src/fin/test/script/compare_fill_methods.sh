#!/usr/bin/env bash
# Compare post-fill exact floating density for greedy fill and two LP objectives.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_dir="$(dirname "$script_dir")"
repo_dir="$(cd "$script_dir/../../../.." && pwd)"
openroad_bin="${OPENROAD_BIN:-$repo_dir/bazel-bin/openroad}"
run_date="$(date +%Y%m%d)"
results_group="${FIN_RESULTS_GROUP:-fill_method_comparison}"
rules_file="${FIN_RULES:-$test_dir/fill.json}"
results_root="$test_dir/results/$run_date/gcd/$results_group"
mkdir -p "$results_root"

if [[ ! -x "$openroad_bin" ]]; then
  echo "OpenROAD executable not found: $openroad_bin" >&2
  exit 1
fi

for method in density lp_minvar min_amount; do
  FIN_FILL_METHOD="$method" FIN_RULES="$rules_file" \
    FIN_RESULTS_GROUP="$results_group" "$openroad_bin" \
    "$script_dir/gcd_fill_method_comparison.tcl" \
    | tee "$results_root/${method}.log"
  shopt -s nullglob
  for profile in "$results_root/$method"/*_met*.json; do
    python3 "$script_dir/plot_floating_density_profile.py" \
      "$profile" "${profile%.json}.svg"
  done
done

python3 - "$results_root" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
print("\nExact floating-density summary (fraction / percent)")
print("method       layer  min       max")
for method in ("density", "lp_minvar", "min_amount"):
    for profile in sorted((root / method).glob("*_met*.json")):
        data = json.loads(profile.read_text())
        print(f"{method:12} {data['layer']:5} {data['min_density']:.6f} "
              f"{data['max_density']:.6f}  "
              f"({data['min_density'] * 100:.2f}% - {data['max_density'] * 100:.2f}%)")
PY

#!/usr/bin/env bash
# Run the GCD MinVar size sweep with PDLP. Extra arguments override defaults.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../../../.." && pwd)"
run_date="$(date +%Y%m%d)"

cd "$repo_dir"
exec python3 "$script_dir/run_gcd_scalability.py" \
  --methods lp_minvar \
  --sizes 1x1 1x2 2x1 2x2 1x10 2x5 5x2 10x1 10x10 20x20 20x50 40x40 100x100 \
  --lp-threads 1 \
  --lp-solver pdlp \
  --rules src/fin/test/fill_met2_only.json \
  --resolution 4 \
  --window 50 \
  --threads 1 \
  --repeats 1 \
  --output-dir "src/fin/test/results/$run_date/gcd_minvar_scaling_r4_pdlp" \
  "$@"

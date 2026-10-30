#!/usr/bin/env bash
# Compare PDLP convergence of min-fill-amount and min-var on 10x10 and 20x20.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../../../.." && pwd)"
run_stamp="$(date +%Y%m%d_%H%M%S)"
output_dir="${SOLVER_CONVERGENCE_OUTPUT_DIR:-$repo_dir/src/fin/test/results/${run_stamp:0:8}/gcd_solver_convergence_$run_stamp}"

cd "$repo_dir"
for method in min_amount lp_minvar; do
  python3 "$script_dir/run_gcd_combined_thread_sweep.py" \
    --sizes 10x10 20x20 \
    --thread-counts 16 \
    --repeats 1 \
    --solver-log \
    "$@" \
    --method "$method" \
    --output-dir "$output_dir/$method"
done

printf '\nSolver convergence results: %s\n' "$output_dir"

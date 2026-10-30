#!/usr/bin/env bash
# Compare GLOP and PDLP thread counts on identical GCD inputs, sequentially.
# Runner options such as --sizes, --repeats, --timeout and --dry-run are forwarded.
# Set LP_THREAD_SWEEP_OUTPUT_DIR to choose the parent results directory.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../../../.." && pwd)"
run_stamp="$(date +%Y%m%d_%H%M%S)"
output_dir="${LP_THREAD_SWEEP_OUTPUT_DIR:-$repo_dir/src/fin/test/results/${run_stamp:0:8}/gcd_pdlp_thread_sweep_$run_stamp}"

cd "$repo_dir"
status=0
for configuration in pdlp:16 pdlp:8 pdlp:1 glop:1; do
  lp_solver="${configuration%:*}"
  lp_threads="${configuration#*:}"
  case_dir="threads_$lp_threads"
  if [[ "$lp_solver" == glop ]]; then
    case_dir="glop_threads_$lp_threads"
  fi
  printf '\nLP solver: %s, thread count: %s\n' "$lp_solver" "$lp_threads"
  if python3 "$script_dir/run_gcd_scalability.py" \
    --methods lp_minvar \
    --sizes 10x10 \
    --rules src/fin/test/fill_met2_only.json \
    --resolution 4 \
    --window 50 \
    --repeats 3 \
    "$@" \
    --lp-solver "$lp_solver" \
    --threads 1 \
    --lp-threads "$lp_threads" \
    --output-dir "$output_dir/$case_dir"; then
    :
  else
    status=1
  fi
done
printf '\nThread sweep results: %s\n' "$output_dir"
exit "$status"

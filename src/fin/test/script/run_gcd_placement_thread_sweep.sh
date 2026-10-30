#!/usr/bin/env bash
# Sweep placement preparation threads; PDLP stays at 16, candidates default to 8.
# Forward runner options; experiments run sequentially, never concurrently.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../../../.." && pwd)"
run_stamp="$(date +%Y%m%d_%H%M%S)"
output_dir="${PLACEMENT_THREAD_SWEEP_OUTPUT_DIR:-$repo_dir/src/fin/test/results/${run_stamp:0:8}/gcd_placement_thread_sweep_$run_stamp}"
read -r -a thread_counts <<< "${PLACEMENT_THREAD_COUNTS:-1 2 4 8 16}"

declare -A seen=()
for threads in "${thread_counts[@]}"; do
  if [[ ! "$threads" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: placement thread counts must be distinct positive integers" >&2
    exit 2
  fi
  if [[ -n "${seen[$threads]:-}" ]]; then
    echo "error: placement thread counts must be distinct positive integers" >&2
    exit 2
  fi
  seen[$threads]=1
done
if [[ -z "${seen[1]:-}" ]]; then
  echo "error: include 1 in PLACEMENT_THREAD_COUNTS as the speedup baseline" >&2
  exit 2
fi

dry_run=false
for argument in "$@"; do
  if [[ "$argument" == --dry-run ]]; then
    dry_run=true
  fi
done

cd "$repo_dir"
status=0
for threads in "${thread_counts[@]}"; do
  printf '\nPDLP threads: 16, placement threads: %s\n' "$threads"
  if python3 "$script_dir/run_gcd_scalability.py" \
    --sizes 10x10 \
    --methods min_amount \
    --rules src/fin/test/fill_met2_only.json \
    --resolution 4 \
    --window 50 \
    --repeats 3 \
    --timeout 1000 \
    --write-reports \
    --candidate-threads 8 \
    "$@" \
    --lp-solver pdlp \
    --lp-threads 16 \
    --threads 1 \
    --placement-threads "$threads" \
    --output-dir "$output_dir/placement_threads_$threads"; then
    :
  else
    return_code=$?
    if (( return_code == 130 || return_code == 143 )); then
      exit "$return_code"
    fi
    status=1
  fi
done
if ! "$dry_run"; then
  if ! python3 "$script_dir/summarize_gcd_placement_thread_sweep.py" "$output_dir"; then
    status=1
  fi
fi
printf '\nPlacement thread sweep results: %s\n' "$output_dir"
exit "$status"

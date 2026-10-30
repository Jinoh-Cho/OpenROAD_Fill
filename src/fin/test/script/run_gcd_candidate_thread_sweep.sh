#!/usr/bin/env bash
# Sweep candidate generation threads; keep PDLP at 16 threads.
# Runner options are forwarded. Experiments run sequentially.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../../../.." && pwd)"
run_stamp="$(date +%Y%m%d_%H%M%S)"
output_dir="${CANDIDATE_THREAD_SWEEP_OUTPUT_DIR:-$repo_dir/src/fin/test/results/${run_stamp:0:8}/gcd_candidate_thread_sweep_$run_stamp}"
read -r -a thread_counts <<< "${CANDIDATE_THREAD_COUNTS:-1 2 4 8 16}"

declare -A seen=()
for threads in "${thread_counts[@]}"; do
  if [[ ! "$threads" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: candidate thread counts must be distinct positive integers" >&2
    exit 2
  fi
  if [[ -n "${seen[$threads]:-}" ]]; then
    echo "error: candidate thread counts must be distinct positive integers" >&2
    exit 2
  fi
  seen[$threads]=1
done
if [[ -z "${seen[1]:-}" ]]; then
  echo "error: include 1 in CANDIDATE_THREAD_COUNTS as the speedup baseline" >&2
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
  printf '\nPDLP threads: 16, candidate threads: %s\n' "$threads"
  if python3 "$script_dir/run_gcd_scalability.py" \
    --sizes 10x10 \
    --methods min_amount \
    --rules src/fin/test/fill_met2_only.json \
    --resolution 4 \
    --window 50 \
    --repeats 3 \
    --timeout 1000 \
    --write-reports \
    "$@" \
    --lp-solver pdlp \
    --lp-threads 16 \
    --threads 1 \
    --candidate-threads "$threads" \
    --output-dir "$output_dir/candidate_threads_$threads"; then
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
  if ! python3 "$script_dir/summarize_gcd_candidate_thread_sweep.py" "$output_dir"; then
    status=1
  fi
fi
printf '\nCandidate thread sweep results: %s\n' "$output_dir"
exit "$status"

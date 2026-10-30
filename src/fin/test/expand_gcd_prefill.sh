#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
# Generate independent tiled designs; share POWER/GROUND special nets.
set -euo pipefail

test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
input="${test_dir}/prefill_bench/gcd_prefill.def"
replicator="${test_dir}/replicate_def.py"
output_dir="${test_dir}/prefill_bench/expanded"
python_bin="${PYTHON:-python3}"
dry_run=false
extension=def
sizes=(2x1 1x2 2x2 2x5 10x10 20x50 100x100)

usage() {
  cat <<'EOF'
Usage: bash expand_gcd_prefill.sh [--output-dir DIR] [--gzip] [--dry-run]

Generate gcd_prefill at 2x1, 1x2, 2x2, 2x5, 10x10, 20x50 and 100x100 (columns x rows).
Default output: prefill_bench/expanded/gcd_prefill_<columns>x<rows>.def
  --output-dir DIR  Write results to DIR instead.
  --gzip            Write compressed .def.gz files instead.
  --dry-run         Print commands without creating files.
  --help            Show this help.
Existing output files are never overwritten. PYTHON selects the interpreter.
EOF
}

while (($#)); do
  case "$1" in
    --output-dir)
      if (($# < 2)) || [[ -z "$2" ]]; then
        echo 'error: --output-dir requires a directory' >&2
        exit 1
      fi
      output_dir="$2"
      shift 2
      ;;
    --gzip) extension=def.gz; shift ;;
    --dry-run) dry_run=true; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

[[ -f "$input" && -f "$replicator" ]] || {
  echo "error: missing input DEF or replicate_def.py in ${test_dir}" >&2
  exit 1
}
command -v "$python_bin" >/dev/null

# Check every destination before starting any of the potentially large jobs.
for size in "${sizes[@]}"; do
  output="${output_dir}/gcd_prefill_${size}.${extension}"
  if [[ -e "$output" || -L "$output" ]]; then
    echo "error: output already exists: $output" >&2
    exit 1
  fi
done

if ! "$dry_run"; then
  mkdir -p -- "$output_dir"
fi

# Run sequentially to limit memory and concurrent disk writes.
for size in "${sizes[@]}"; do
  nx="${size%x*}"
  ny="${size#*x}"
  output="${output_dir}/gcd_prefill_${size}.${extension}"
  command_args=("$python_bin" "$replicator" "$input" "$output"
                --nx "$nx" --ny "$ny" --share-power)
  if "$dry_run"; then
    printf '%q ' "${command_args[@]}"
    printf '\n'
  else
    echo "Generating ${size}: ${output}"
    "${command_args[@]}"
  fi
done

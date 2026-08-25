#!/usr/bin/env bash
# Measure fill-area rectangle extraction at multiple horizontal copy counts.
# Usage: ./run_fin_rectangle_scaling.sh [runs_per_scale]

set -euo pipefail

runs="${1:-1}"
if ! [[ "$runs" =~ ^[1-9][0-9]*$ ]]; then
  echo "runs_per_scale must be a positive integer." >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bazel_root="${OPENROAD_BAZEL_ROOT:-/tmp/${USER:-user}/openroad_bazel}"
copy_counts=(1 10 100 1000 10000 100000)

if ! command -v module >/dev/null 2>&1 && [[ -f /etc/profile.d/modules.sh ]]; then
  # shellcheck source=/etc/profile.d/modules.sh
  source /etc/profile.d/modules.sh
fi
if ! command -v module >/dev/null 2>&1; then
  echo "Environment Modules is unavailable; load openroad-deps/20260625 first." >&2
  exit 1
fi

module load openroad-deps/20260625

cd "$repo_root"
git submodule sync --recursive
git submodule update --init --recursive

mkdir -p "$bazel_root"
bazelisk --output_user_root="$bazel_root" \
  build --jobs="$(nproc)" //:openroad

for copies in "${copy_counts[@]}"; do
  right_copies=$((copies - 1))
  echo "===== FIN rectangle extraction: copies=${copies}, runs=${runs} ====="

  (
    cd "$repo_root/src/fin/test"
    FIN_BENCH_LEFT=0 \
    FIN_BENCH_RIGHT="$right_copies" \
    FIN_BENCH_RUNS="$runs" \
      "$repo_root/bazel-bin/openroad" \
        -no_splash -no_init -exit gcd_fill_rectangle_benchmark.tcl
  )
done

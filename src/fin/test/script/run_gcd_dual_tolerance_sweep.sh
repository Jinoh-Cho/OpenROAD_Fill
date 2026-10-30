#!/usr/bin/env bash
# Dual-only 1e-6 versus 1e-5, both objectives, 10x10/20x20, one run each.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$script_dir/run_gcd_dual_tolerance_sweep.py" "$@"

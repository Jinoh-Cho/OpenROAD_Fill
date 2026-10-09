#!/usr/bin/env python3
"""Compare Density, MinVar, MinFillAmount, and Lip1/2/3 on fresh IBEX layouts."""

import sys

from run_gcd_six_method_comparison import main


if __name__ == "__main__":
    sys.exit(main("ibex"))

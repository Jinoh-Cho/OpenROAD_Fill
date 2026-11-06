# Candidate summary and SVG runtime sweep

Rebuild OpenROAD after the candidate summary changes before using this script.
Existing running experiments are unaffected. The script uses existing TPU ODBs;
it does not generate inputs. Python 3.6+ is supported.

```bash
python3 /home/jocho/OpenFill/OpenROAD_Fill/src/fin/test/script/run_candidate_summary_sweep.py --size 10x10 --methods min_amount lp_minvar --repeats 1
```

Each method runs four sequential fresh processes:

| SVG | Candidate summary threads | Candidate / Placement / PDLP threads |
|---|---:|---|
| OFF | 1 | 16 / 16 / 16 |
| OFF | 16 | 16 / 16 / 16 |
| ON | 1 | 16 / 16 / 16 |
| ON | 16 | 16 / 16 / 16 |

Default size is 4x4 and default method is min_amount. Use `--dry-run` to see
settings without running experiments. `--input-dir`, `--openroad`, `--rules`,
`--summary-thread-counts`, `--candidate-threads`, `--placement-threads` and
`--lp-threads` override defaults. OpenROAD global threads remain 1.

`comparison.md`, `comparison.csv`, `summary.json` and per-case logs/reports/SVGs
are saved below the printed results directory. Summary seconds are wall time,
including final geometry release and, with SVG on, polygon/area extraction and
ordered concatenation. SVG writing is reported separately. Fill call total
includes SVG writing. Parent totals contain subphases; do not sum every row.
Reports include summary speedup/saved seconds against 1T and fill count/area
consistency across successful cases. Runtime is subject to cache and host load;
repeat with `--repeats 3` when a stable speed comparison is needed.

This measures the updated implementation with SVG off/on; it is not a comparison
against the pre-optimization binary. Summary thread control is independent:
`FIN_CANDIDATE_SUMMARY_THREADS` overrides the usual candidate-thread default.
The worker explicitly sets this value and `FIN_WRITE_SVG`, records them in its
manifest, and disables detailed tracing. GUI or detailed candidate tracing forces
the summary to run serially. The report displays the actual summary thread count.

To regenerate reports:

```bash
python3 /home/jocho/OpenFill/OpenROAD_Fill/src/fin/test/script/run_candidate_summary_sweep.py --summarize-only --output-dir /path/to/results --methods min_amount lp_minvar
```

Use the original methods and summary thread counts when summarizing.

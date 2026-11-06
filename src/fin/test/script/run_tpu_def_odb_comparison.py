#!/usr/bin/env python3
"""Compare existing TPU DEF/ODB inputs with matching fill and thread settings.

Defaults: 10x10, MIN AMOUNT/MIN VAR, candidate/placement16/16 and32/32,
PDLP16, one repeat, all cases sequential. Does not generate or overwrite inputs.
"""
import argparse
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
WORKSPACE = REPO.parent
STAGES = [
    ('Input loading', 'load_seconds'),
    ('Initial geometry/density and other compute', 'initial_other_seconds'),
    ('Candidate preparation total', 'candidates_total_seconds'),
    ('Metal spacing expansion', 'candidate_bloat_seconds'),
    ('Free-area subtraction', 'candidate_free_subtract_seconds'),
    ('Rectangle decomposition', 'candidate_free_decompose_seconds'),
    ('Tile distribution', 'candidate_tile_partition_seconds'),
    ('Tile spacing', 'candidate_tile_spacing_seconds'),
    ('Tile pruning', 'candidate_tile_prune_seconds'),
    ('Candidate generation', 'candidate_generation_seconds'),
    ('Candidate summary', 'candidate_summary_seconds'),
    ('LP solving', 'lp_solve_seconds'),
    ('Selection', 'placement_select_seconds'),
    ('Shape generation', 'placement_shape_seconds'),
    ('Collection', 'placement_collect_seconds'),
    ('Merge', 'placement_merge_seconds'),
    ('DB insertion', 'db_insert_seconds'),
    ('Post-fill density', 'post_fill_density_seconds'),
    ('Placement total', 'placement_seconds'),
    ('Fill call total', 'fill_seconds'),
    ('Process wall total', 'wall_seconds'),
    ('Solver iterations', 'lp_solver_iterations'),
    ('Fill count', 'fill_count'),
]


def summarize(root, size, threads, methods):
    records = []
    builds = set()
    for fmt in ('def', 'odb'):
        for count in threads:
            folder = root / fmt / ('candidate_%d_placement_%d' % (count, count))
            config = folder / 'experiment.json'
            if config.exists():
                builds.add(json.loads(config.read_text()).get('openroad_sha256'))
            path = folder / 'summary.csv'
            if not path.exists():
                continue
            with path.open() as stream:
                rows = list(csv.DictReader(stream))
            for row in rows:
                if row['size'] != size or row['method'] not in methods:
                    continue
                row.update(format=fmt, requested_threads=count)
                if row.get('status') == 'ok' and row.get('log'):
                    report = Path(row['log']).parent / 'lp_fill_report.json'
                    if report.exists():
                        runtime = [layer['runtime_seconds'] for layer in json.loads(report.read_text())['layers']]
                        row['candidates_total_seconds'] = sum(r['candidates'] for r in runtime)
                        row['lp_solve_seconds'] = sum(r['lp_solve'] for r in runtime)
                        row['initial_other_seconds'] = sum(r['compute'] - r['candidates'] - r['lp_solve'] - r['placement'] for r in runtime)
                records.append(row)

    def median(fmt, count, method, key):
        values = [float(r[key]) for r in records if r['format'] == fmt
                  and r['requested_threads'] == count and r['method'] == method
                  and r['status'] == 'ok' and r.get(key) not in (None, '')]
        return statistics.median(values) if values else None

    same_build = len(builds) == 1 and None not in builds
    lines = ['# TPU %s DEF / ODB runtime comparison' % size, '',
             'Wall-clock seconds; totals contain their subphases. Do not sum all rows.',
             'Successful matching-thread runs only; values are medians when repeated.',
             'DEF loading includes LEFs; ODB loading skips LEF preloading.',
             'Existing files are used unchanged; cache, host load and source object order are not controlled.',
             'Identical recorded OpenROAD build: %s.' % same_build, '']
    comparison = []
    for method in methods:
        columns = [(fmt, count) for count in threads for fmt in ('def', 'odb')]
        lines += ['## ' + method, '',
                  '| Stage | ' + ' | '.join('%s C%d/P%d' % (fmt.upper(), t, t) for fmt, t in columns) + ' |',
                  '|---|' + '---:|' * len(columns)]
        for label, key in STAGES:
            values = [median(fmt, t, method, key) for fmt, t in columns]
            lines.append('| %s | %s |' % (label, ' | '.join('—' if v is None else '%g' % v for v in values)))
            for count in threads:
                a, b = (median(fmt, count, method, key) for fmt in ('def', 'odb'))
                comparison.append({'method': method, 'candidate_threads': count,
                                   'placement_threads': count, 'metric': key,
                                   'def_value': a, 'odb_value': b,
                                   'saved_def_minus_odb': a - b if a is not None and b is not None else None,
                                   'speedup_def_over_odb': a / b if same_build and a is not None and b else None})
        lines += ['', 'Status: ' + '; '.join('%s C%d/P%d: %s' % (
            fmt.upper(), t, t, ', '.join(r['status'] for r in records if r['format'] == fmt
            and r['requested_threads'] == t and r['method'] == method) or 'not run')
            for fmt, t in columns), '']
    (root / 'comparison.md').write_text('\n'.join(lines) + '\n')
    (root / 'summary.json').write_text(json.dumps(records, indent=2) + '\n')
    with (root / 'comparison.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(comparison[0]))
        writer.writeheader(); writer.writerows(comparison)
    print('Combined comparison: ' + str(root / 'comparison.md'), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--size', default='10x10')
    p.add_argument('--def-dir', type=Path, default=WORKSPACE / 'benchmarks/asap7_tpu_orfs/expanded_python')
    p.add_argument('--odb-dir', type=Path, default=WORKSPACE / 'benchmarks/asap7_tpu_orfs/expanded_odb_native')
    p.add_argument('--thread-counts', nargs='+', type=int, default=[16, 32])
    p.add_argument('--methods', nargs='+', choices=('min_amount', 'lp_minvar'), default=['min_amount', 'lp_minvar'])
    p.add_argument('--lp-threads', type=int, default=16)
    p.add_argument('--repeats', type=int, default=1)
    p.add_argument('--output-dir', type=Path)
    p.add_argument('--openroad', type=Path, default=Path(os.environ.get('OPENROAD_BIN', str(REPO / 'bazel-bin/openroad'))))
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--summarize-only', action='store_true')
    args = p.parse_args()
    if not re.fullmatch(r'[1-9]\d*x[1-9]\d*', args.size):
        p.error('--size must be columns x rows')
    if min(args.thread_counts + [args.lp_threads, args.repeats]) < 1:
        p.error('Threads and repeats must be positive')
    if len(set(args.thread_counts)) != len(args.thread_counts) or len(set(args.methods)) != len(args.methods):
        p.error('Duplicate threads/methods are not allowed')
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    root = (args.output_dir or REPO / 'src/fin/test/results' / stamp[:8]
            / ('tpu_def_odb_comparison_' + stamp)).resolve()
    if args.summarize_only:
        if not root.is_dir():
            p.error('Existing --output-dir required for --summarize-only')
        summarize(root, args.size, args.thread_counts, args.methods)
        return 0
    inputs = {'def': args.def_dir.resolve(), 'odb': args.odb_dir.resolve()}
    for fmt, folder in inputs.items():
        path = folder / ('tpu_orfs_' + args.size + '.' + fmt)
        if not path.is_file():
            p.error('Input not found: ' + str(path))
    if root.exists():
        p.error('Output directory exists; choose a new --output-dir')
    print('Results: ' + str(root), flush=True)
    commands = []
    for fmt in ('def', 'odb'):
        command = ['bash', str(WORKSPACE / 'scripts/run_tpu_combined_thread_sweep.sh'),
                   '--sizes', args.size, '--input-format', fmt, '--input-dir', str(inputs[fmt]),
                   '--baseline-def', str(inputs[fmt] / ('tpu_orfs_' + args.size + '.' + fmt)),
                   '--thread-counts'] + [str(t) for t in args.thread_counts]
        command += ['--methods'] + args.methods + ['--lp-threads', str(args.lp_threads),
                    '--repeats', str(args.repeats), '--openroad', str(args.openroad.resolve())]
        commands.append((fmt, command))
    if not args.dry_run:
        root.mkdir(parents=True)
        (root / 'comparison_config.json').write_text(json.dumps({
            'size': args.size, 'thread_counts': args.thread_counts, 'methods': args.methods,
            'lp_threads': args.lp_threads, 'repeats': args.repeats,
            'input_dirs': {k: str(v) for k, v in inputs.items()},
            'input_files': {fmt: {'path': str(folder / ('tpu_orfs_' + args.size + '.' + fmt)),
                                  'bytes': (folder / ('tpu_orfs_' + args.size + '.' + fmt)).stat().st_size,
                                  'mtime_ns': (folder / ('tpu_orfs_' + args.size + '.' + fmt)).stat().st_mtime_ns}
                            for fmt, folder in inputs.items()},
            'order': ['def', 'odb'], 'preparation': 'existing files only; no conversion'}, indent=2) + '\n')
    status = 0
    for fmt, command in commands:
        env = os.environ.copy()
        env['TPU_THREAD_SWEEP_OUTPUT_DIR'] = str(root / fmt)
        if args.dry_run:
            command.append('--dry-run')
        print('FORMAT=' + fmt.upper(), flush=True)
        code = subprocess.call(command, env=env)
        if code in (130, 143, -2, -15):
            return 130
        status = status or code
        if not args.dry_run:
            summarize(root, args.size, args.thread_counts, args.methods)
    return status


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)

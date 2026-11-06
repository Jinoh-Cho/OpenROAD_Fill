#!/usr/bin/env python3
"""Compare independent candidate-summary thread counts with SVG disabled.

Fresh sequential OpenROAD processes; candidate generation, placement and PDLP
threads stay fixed. Uses existing TPU ODB inputs, without conversion.
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
SVG_MODES = (False,)
METRICS = [
    ('Actual metal collection threads', 'metal_collect_threads'),
    ('Spacing fused into collection (1=yes)', 'metal_spacing_fused'),
    ('Tile-local free-area preparation (1=yes)', 'candidate_free_area_tiled'),
    ('Collection enumeration (included in collection phases)', 'initial_collect_enumerate_seconds'),
    ('Collection merging (included in collection phases)', 'initial_collect_merge_seconds'),
    ('Initial regular wire collection', 'initial_collect_regular_seconds'),
    ('Initial special wire/via collection', 'initial_collect_special_seconds'),
    ('Initial instance geometry collection', 'initial_collect_instances_seconds'),
    ('Initial metal collection total (includes fused spacing)', 'initial_collect_total_seconds'),
    ('Initial metal union/clip', 'initial_union_clip_seconds'),
    ('Initial rectangle decomposition', 'initial_decompose_seconds'),
    ('Initial tile area accumulation', 'initial_tile_accumulation_seconds'),
    ('Initial window density', 'initial_window_seconds'),
    ('Initial density total', 'initial_density_total_seconds'),
    ('Initial density threads', 'initial_density_threads'),
    ('Tile-local initial density (1=yes)', 'initial_density_tiled'),
    ('Initial density rectangle distribution', 'initial_partition_seconds'),
    ('Initial tile union / area', 'initial_tile_union_area_seconds'),
    ('Initial density buffer cleanup', 'initial_density_cleanup_seconds'),
    ('Initial source input cleanup', 'initial_input_cleanup_seconds'),
    ('Actual summary threads', 'candidate_summary_threads'),
    ('Candidate summary', 'candidate_summary_seconds'),
    ('SVG writing', 'svg_seconds'),
    ('Separate metal spacing expansion (0 when prepared during collection)', 'candidate_bloat_seconds'),
    ('Free-area subtraction / expanded union', 'candidate_free_subtract_seconds'),
    ('Free-area rectangle decomposition', 'candidate_free_decompose_seconds'),
    ('Rectangle distribution to tiles', 'candidate_tile_partition_seconds'),
    ('Tile spacing', 'candidate_tile_spacing_seconds'),
    ('Tile pruning', 'candidate_tile_prune_seconds'),
    ('Candidate generation', 'candidate_generation_seconds'),
    ('Candidate preparation total', 'candidates_total_seconds'),
    ('LP solving', 'lp_solve_seconds'),
    ('Placement total', 'placement_seconds'),
    ('Fill call total (includes SVG)', 'fill_seconds'),
    ('Input loading', 'load_seconds'),
    ('Framework TOTAL (load + fill + postprocessing + exit)', 'framework_total_seconds'),
]


def summarize(root, methods, counts):
    records = []
    for svg in SVG_MODES:
        for count in counts:
            path = root / ('svg_%s_summary_%d' % ('on' if svg else 'off', count)) / 'summary.csv'
            if not path.exists():
                continue
            with path.open() as stream:
                for row in csv.DictReader(stream):
                    row['framework_total_seconds'] = row.get('framework_total_seconds') or row.get('wall_seconds', '')
                    row.update(svg=svg, requested_summary_threads=count)
                    if row.get('status') == 'ok':
                        report = Path(row['log']).parent / 'lp_fill_report.json'
                        if report.exists():
                            runtime = [layer['runtime_seconds'] for layer in json.loads(report.read_text())['layers']]
                            for field, key in [('svg_seconds', 'svg'), ('candidates_total_seconds', 'candidates'),
                                               ('lp_solve_seconds', 'lp_solve')]:
                                row[field] = sum(r[key] for r in runtime)
                        if not row.get('candidate_summary_threads'):
                            row['status'] = 'missing_summary_thread_log_rebuild_required'
                    records.append(row)
    columns = [(svg, count) for svg in SVG_MODES for count in counts]
    def value(method, svg, count, key):
        values = [float(r[key]) for r in records if r['method'] == method
                  and r['svg'] == svg and r['requested_summary_threads'] == count
                  and r['status'] == 'ok' and r.get(key) not in ('', None)]
        return statistics.median(values) if values else None
    lines = ['# Candidate summary / SVG comparison', '',
             'Elapsed wall seconds; medians across successful repeats. Totals include subphases.',
             'Candidate generation, placement and LP thread settings are fixed.',
             'SVG output and its geometry extraction are disabled for every case.', '']
    for method in methods:
        lines += ['## ' + method, '', '| Stage | ' + ' | '.join(
            'SVG %s / summary %dT' % ('ON' if svg else 'OFF', count) for svg, count in columns) + ' |',
            '|---|' + '---:|' * len(columns)]
        for title, key in METRICS:
            values = [value(method, svg, count, key) for svg, count in columns]
            lines.append('| %s | %s |' % (title, ' | '.join('—' if v is None else '%.6f' % v for v in values)))
        lines += ['', '| SVG | Summary threads | Summary speedup vs 1T | Saved seconds vs 1T |',
                  '|---|---:|---:|---:|']
        for svg, count in columns:
            baseline = value(method, svg, 1, 'candidate_summary_seconds')
            current = value(method, svg, count, 'candidate_summary_seconds')
            lines.append('| %s | %d | %s | %s |' % (
                'ON' if svg else 'OFF', count,
                '—' if baseline is None or not current else '%.3fx' % (baseline / current),
                '—' if baseline is None or current is None else '%.6f' % (baseline - current)))
        subset = [r for r in records if r['method'] == method]
        successful = [r for r in subset if r['status'] == 'ok']
        signatures = {(r.get('fill_count'), r.get('placed_fill_area_dbu2')) for r in successful}
        lines += ['', 'Fill count / area match across successful runs: %s.' % (len(signatures) == 1 if successful else 'no successful runs'),
                  'Statuses: ' + ', '.join('%s=%d' % (status, sum(r['status'] == status for r in subset))
                                           for status in sorted({r['status'] for r in subset})), '']
    (root / 'comparison.md').write_text('\n'.join(lines) + '\n')
    (root / 'summary.json').write_text(json.dumps(records, indent=2) + '\n')
    if records:
        fields = sorted({key for row in records for key in row})
        with (root / 'comparison.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader(); writer.writerows(records)
    print('Report: ' + str(root / 'comparison.md'), flush=True)
    return all(r['status'] == 'ok' for r in records) and bool(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', default='4x4')
    parser.add_argument('--input-dir', type=Path, default=WORKSPACE / 'benchmarks/asap7_tpu_orfs/expanded_odb_native')
    parser.add_argument('--summary-thread-counts', nargs='+', type=int, default=[1, 16])
    parser.add_argument('--candidate-threads', type=int, default=16)
    parser.add_argument('--serial-metal-collection', action='store_true',
                        help='Experiment: force only initial metal collection to 1 thread')
    parser.add_argument('--placement-threads', type=int, default=16)
    parser.add_argument('--lp-threads', type=int, default=16)
    parser.add_argument('--methods', nargs='+', choices=['min_amount', 'lp_minvar'], default=['min_amount'])
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--openroad', type=Path, default=Path(os.environ.get('OPENROAD_BIN', str(REPO / 'bazel-bin/openroad'))))
    parser.add_argument('--rules', type=Path, default=WORKSPACE / 'scripts/fill_asap7_m2_thread_sweep.json')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--summarize-only', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'[1-9]\d*x[1-9]\d*', args.size):
        parser.error('--size must be columns x rows')
    if min(args.summary_thread_counts + [args.candidate_threads, args.placement_threads, args.lp_threads, args.repeats]) < 1:
        parser.error('Thread counts and repeats must be positive')
    if len(set(args.summary_thread_counts)) != len(args.summary_thread_counts) or len(set(args.methods)) != len(args.methods):
        parser.error('Duplicate thread counts/methods')
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    root = (args.output_dir or REPO / 'src/fin/test/results' / stamp[:8] / ('candidate_summary_sweep_' + stamp)).resolve()
    if args.summarize_only:
        if not args.output_dir or not root.is_dir():
            parser.error('--summarize-only requires an existing --output-dir')
        return 0 if summarize(root, args.methods, args.summary_thread_counts) else 1
    input_file = args.input_dir.resolve() / ('tpu_orfs_' + args.size + '.odb')
    if not input_file.is_file():
        parser.error('ODB not found: ' + str(input_file))
    if root.exists():
        parser.error('Output directory already exists')
    print('Results: ' + str(root), flush=True)
    if not args.dry_run:
        root.mkdir(parents=True)
        config = {key: str(val) if isinstance(val, Path) else val for key, val in vars(args).items()}
        (root / 'sweep_config.json').write_text(json.dumps(config, indent=2) + '\n')
    status = 0
    for svg in SVG_MODES:
        for count in args.summary_thread_counts:
            label = 'svg_%s_summary_%d' % ('on' if svg else 'off', count)
            command = [sys.executable, str(HERE / 'run_gcd_scalability.py'),
                       '--benchmark', 'tpu', '--input-format', 'odb', '--input-prefix', 'tpu_orfs_',
                       '--input-dir', str(args.input_dir.resolve()), '--baseline-def', str(input_file),
                       '--sizes', args.size, '--methods'] + args.methods + [
                       '--candidate-threads', str(args.candidate_threads), '--candidate-summary-threads', str(count),
                       '--placement-threads', str(args.placement_threads), '--lp-solver', 'pdlp', '--lp-threads', str(args.lp_threads),
                       '--threads', '1', '--resolution', '4', '--window', '50', '--repeats', str(args.repeats),
                       '--write-reports', '--solver-log', '--openroad', str(args.openroad.resolve()),
                       '--rules', str(args.rules.resolve()), '--output-dir', str(root / label)]
            if svg:
                command.append('--write-svg')
            if args.serial_metal_collection:
                command.append('--serial-metal-collection')
            if args.dry_run:
                command.append('--dry-run')
            print(label, flush=True)
            code = subprocess.call(command)
            if not args.dry_run:
                summarize(root, args.methods, args.summary_thread_counts)
                records = json.loads((root / 'summary.json').read_text())
                if any(r['status'] == 'missing_summary_thread_log_rebuild_required' for r in records):
                    print('Missing candidate_summary_threads log: rebuild OpenROAD with the new summary code.', flush=True)
                    return 1
            if code in (130, 143, -2, -15):
                return 130
            status = status or code
    if not args.dry_run and not summarize(root, args.methods, args.summary_thread_counts):
        status = 1
    return status


if __name__ == '__main__':
    sys.exit(main())

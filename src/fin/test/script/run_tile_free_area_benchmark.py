#!/usr/bin/env python3
"""Compare global/tiled free areas, initial density, or candidate optimization.

Uses a built C++ test binary and an existing ODB. --mode free measures area
preparation at1T/16T; density and candidate measure at1T/16T/32T. Three trials
per mode, alternating order, with exact geometry/candidate checks. Loading,
common preparation, reference copies and correctness checks are excluded.
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

REPO = Path(__file__).resolve().parents[4]


def summarize(log, output, mode="free"):
    density = mode == "density"
    candidate = mode == "candidate"
    rows = []
    current = None
    for line in log.read_text(errors='replace').splitlines():
        if line.startswith('CANDIDATE_FINAL_SIZE_BENCH:' if candidate else ('INITIAL_DENSITY_BENCH:' if density else 'TILE_FREE_BENCH:')):
            current = {key: float(value) for key, value in re.findall(r'(\w+)=([0-9.eE+-]+)', line)}
            if density:
                current["global_seconds"] = current["legacy_seconds"]
            if candidate:
                current["global_seconds"] = current["old_seconds"]
                current["tiled_seconds"] = current["new_seconds"]
            rows.append(current)
        elif current is not None and line.startswith(('GLOBAL ', 'TILED ')):
            prefix, item = line.split(' ', 1)
            key, value = item.split('=', 1)
            current[prefix.lower() + '_' + key] = float(value)
    if not rows:
        raise ValueError('No completed benchmark trials in ' + str(log))
    scope = 'initial metal density' if density else 'free-area preparation'
    title = '# Candidate final-size update optimization' if candidate else '# Global / tile-local ' + scope
    lines = [title, '',
             'Seconds are wall-time medians. Each phase is measured independently;',
             'phase medians need not sum exactly to median totals.',
             ('Measures candidate generation only; last-size update is the only algorithm change.' if candidate else
              'Includes distribution, tile union/area, cleanup and window density; legacy is serial.' if density
              else 'Includes distribution, free subtraction, tile spacing and pruning.'),
             'Excludes loading/common collection/correctness checks/LP/placement.', '',
             ('| Threads | Trials | Before | After | Speedup | Saved seconds |' if candidate else
              '| Threads | Trials | Global total | Tile-local total | Speedup | Saved seconds |'),
             '|---|---:|---:|---:|---:|---:|']
    for threads in sorted({int(r['threads']) for r in rows}):
        subset = [r for r in rows if int(r['threads']) == threads]
        def med(key):
            return statistics.median(r[key] for r in subset)
        a,b = med('global_seconds'),med('tiled_seconds')
        lines.append('| %d | %d | %.6f | %.6f | %.3fx | %.6f |' % (threads,len(subset),a,b,a/b,a-b))
    for threads in sorted({int(r['threads']) for r in rows}):
        if candidate:
            continue
        subset = [r for r in rows if int(r['threads']) == threads]
        lines += ['', '## %d threads' % threads, '', '| Phase | Global | Tile-local |', '|---|---:|---:|']
        phases = ([('Source distribution','post_fill_partition_seconds'),
                   ('Tile union/area','post_fill_tile_union_area_seconds'),
                   ('Tile buffer cleanup','post_fill_density_cleanup_seconds'),
                   ('Window density','post_fill_window_seconds')] if density else
                  [('Distribution','candidate_tile_partition_seconds'),
                   ('Subtraction/union','candidate_free_subtract_seconds'),
                   ('Global rectangle decomposition','candidate_free_decompose_seconds'),
                   ('Tile spacing','candidate_tile_spacing_seconds'),
                   ('Tile pruning','candidate_tile_prune_seconds')])
        for title,key in phases:
            values=[]
            for prefix in ['global','tiled']:
                measured=[r[prefix+'_'+key] for r in subset if prefix+'_'+key in r]
                values.append('%.6f' % statistics.median(measured) if measured else '—')
            lines.append('| %s | %s | %s |' % (title,values[0],values[1]))
    (output/'comparison.md').write_text('\n'.join(lines)+'\n')
    (output/'trials.json').write_text(json.dumps(rows,indent=2)+'\n')
    with (output/'trials.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=sorted({k for r in rows for k in r}))
        writer.writeheader();writer.writerows(rows)
    print('\n'.join(lines),flush=True)
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('--mode',choices=['free','density','candidate'],default='free')
    parser.add_argument('--binary',type=Path,default=REPO/'bazel-bin/src/fin/test/tile_free_areas/tile_free_areas_test')
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--thread-counts',type=int,nargs='+',help='Candidate benchmark thread counts (default: 1 16 32)')
    args=parser.parse_args()
    if args.thread_counts and (args.mode != 'candidate' or min(args.thread_counts) < 1):
        parser.error('--thread-counts requires --mode candidate and positive counts')
    if not args.input.is_file():parser.error('ODB not found: '+str(args.input))
    if not args.binary.is_file():parser.error('Build //src/fin/test/tile_free_areas:tile_free_areas_test first')
    stamp=datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    output=(args.output_dir or REPO/'src/fin/test/results'/stamp[:8]/('tile_'+args.mode+'_benchmark_'+stamp)).resolve()
    if output.exists():parser.error('Output directory already exists')
    output.mkdir(parents=True)
    env=os.environ.copy();env.update(FIN_METAL_TEST_ODB=str(args.input.resolve()),OMP_DYNAMIC='FALSE',FIN_TRACE_CANDIDATES='0')
    env.pop('FIN_CANDIDATE_BENCH_THREADS',None)
    if args.thread_counts:
        env['FIN_CANDIDATE_BENCH_THREADS']=' '.join(map(str,args.thread_counts))
    test = ('CandidateFinalSizeTest' if args.mode == 'candidate' else
            'InitialDensityTest' if args.mode == 'density' else 'TileFreeAreasTest')
    command=[str(args.binary.resolve()),'--gtest_filter='+test+'.BenchmarkExistingSmallOdb']
    metadata={'input':str(args.input.resolve()),'bytes':args.input.stat().st_size,
              'mtime_ns':args.input.stat().st_mtime_ns,'binary':str(args.binary.resolve()),
              'command':command,'mode':args.mode,'trials_per_mode':3,'threads':(args.thread_counts or ([1,16,32] if args.mode in ('density','candidate') else [1,16]))}
    (output/'experiment.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('Results: '+str(output),flush=True)
    with (output/'benchmark.log').open('w') as log:
        result=subprocess.call(command,env=env,stdout=log,stderr=subprocess.STDOUT,cwd=str(REPO))
    if result:
        print('Benchmark failed; inspect '+str(output/'benchmark.log'),file=sys.stderr)
        return result
    summarize(output/'benchmark.log',output,args.mode)
    print('Geometry and candidate equality checks passed.',flush=True)
    return 0


if __name__=='__main__':
    sys.exit(main())

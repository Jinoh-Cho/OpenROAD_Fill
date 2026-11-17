#!/usr/bin/env python3
"""Compare M2 quality on public-CI GCD geometry and original TPU geometry.

One fresh process per method. All geometry metrics use the same full union
density analyzer, window50um, step12.5um. Capacity-limited lower bounds, if
used by the binary, are reported explicitly; violations retain requested limits.
"""
import argparse
import csv
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[4]
WORKSPACE = REPO.parent


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def summarize(output):
    rows = []
    for path in sorted(output.glob('*/*/invocation.json')):
        run = path.parent
        invocation = json.loads(path.read_text())
        row = dict(case=invocation['case'], method=invocation['method'], relaxed_window_minimum_count=0)
        row.update(json.loads((run/'process.json').read_text()))
        if (run/'result.json').exists():
            result = json.loads((run/'result.json').read_text())
            row.update(result)
            # FIN can write an infeasible LP report while returning Tcl success.
            # Process exit status alone is not a solved-model certificate.
            if row['method'] != 'density':
                report = run/'lp_fill_report.json'
                if report.exists():
                    lp = next(x for x in json.loads(report.read_text())['layers']
                              if x['layer'] == invocation['layer'])
                    row['lp_solved'] = lp['solved']
                    row['solver_iterations'] = lp.get('solver_iterations')
                    row['relaxed_window_minimum_count'] = lp.get('relaxed_window_minimum_count',0)
                    if not lp['solved']:
                        row['status'] = lp.get('status','unsolved')
                        row['error'] = lp.get('failure',{}).get('reason','LP not solved')
                else:
                    row['status'] = 'missing_lp_report'
            if row['status'] == 'ok' and row['returncode'] == 0:
                data = json.loads((run/'after_density.json').read_text())
                layer = next(x for x in data['layers'] if x['layer'] == invocation['layer'])
                row.update(layer)
                row['range_pp'] = (layer['max_density']-layer['min_density'])*100
                row['min_percent'] = layer['min_density']*100
                row['max_percent'] = layer['max_density']*100
                row['mean_percent'] = layer['mean_density']*100
                row['fill_area_um2'] = result['added_union_area_dbu2']/result['dbu']**2
                features = json.loads((run/'features.json').read_text())
                row['fill_count'] = next(x['fill_count'] for x in features['feature_counts_by_layer'] if x['layer'] == invocation['layer'])
                if row['method'] != 'density':
                    row['placed_vs_union_error_dbu2'] = result['lp_placed_fill_area_dbu2']-result['added_union_area_dbu2']
            (run/'outcome.json').write_text(json.dumps(row,indent=2)+'\n')
        if (run/'failure.txt').exists():
            row['error'] = (run/'failure.txt').read_text().strip()
        rows.append(row)
    (output/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
    with (output/'summary.csv').open('w',newline='') as stream:
        fields = sorted({k for row in rows for k in row})
        writer = csv.DictWriter(stream,fieldnames=fields)
        writer.writeheader(); writer.writerows(rows)
    lines = ['# M2 fill quality comparison','',
             'Fresh process per method; one run per configuration. Same input/rules within each case.',
             'Core region, grid pattern, window50um, resolution4, step12.5um. LP tile bounds20–100%, window bounds30–60%, PDLP16, candidate/placement32, greedy.',
             'Existing density_fill has no window-density target API; the30–60% band is a common evaluation criterion for all methods.',
             'All post-fill metrics use the same full geometric union analyzer. Requested density limits are unchanged in evaluation; capacity-capped LP lower bounds are counted separately. Range is max minus min, in percentage points.',
             'Fill amount is net union metal area added inside the comparison region, in um^2. Counts are M2 dbFill rectangles.',
             '', '| Case | Method | Status | Min % | Max % | Range pp | Mean % | Added fill um^2 | Fill count | Below30% | Above60% | Relaxed minimums |',
             '|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        vals = [r['case'],r['method'],r.get('status','failed')]
        for key in ['min_percent','max_percent','range_pp','mean_percent','fill_area_um2','fill_count','min_violation_window_count','max_violation_window_count','relaxed_window_minimum_count']:
            value = r.get(key)
            vals.append('—' if value is None else (str(value) if key.endswith('count') else '%.6f'%value))
        lines.append('| '+' | '.join(vals)+' |')
    lines += ['', 'Infeasible LPs have no successful fill result: unchanged pre-fill geometry is not reported as an optimized density result, even if OpenROAD exits zero. Inspect the LP solved/status fields and diagnostics.', '', 'GCD input is byte-identical (Git blob SHA d2b5ebd2f985633db1098daecdcdddb4e991fb5e) to the upstream OpenROAD gcd_fill CI input. The upstream rule case uses its exact M2 rule subset; the local rule case is explicitly a separate controlled experiment. TPU is the original ASAP7 routed input, not a replicated layout. These are density/area experiments, not a complete DRC, parasitic or timing signoff evaluation.']
    (output/'comparison.md').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines),flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openroad',type=Path,default=REPO/'bazel-bin/openroad')
    parser.add_argument('--upstream-m2-rules',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--timeout',type=float,default=600)
    parser.add_argument('--dry-run',action='store_true')
    parser.add_argument('--cases',nargs='+',choices=['gcd_ci_rules','gcd_local_rules','tpu_1x1'],help='Default: all three cases')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    output = (args.output_dir or REPO/'src/fin/test/results'/datetime.now().strftime('%Y%m%d')/('m2_quality_comparison_'+datetime.now().strftime('%H%M%S'))).resolve()
    orfs = Path('/home/jocho/OpenROAD-flow-scripts/flow/platforms/asap7/lef')
    gcd_lefs = [REPO/'src/fin/test/sky130hd/sky130hd.tlef',REPO/'src/fin/test/sky130hd/sky130_fd_sc_hd_merged.lef']
    cases = [
        ('gcd_ci_rules', REPO/'src/fin/test/prefill_bench/gcd_prefill.def',gcd_lefs,args.upstream_m2_rules,'met2'),
        ('gcd_local_rules', REPO/'src/fin/test/prefill_bench/gcd_prefill.def',gcd_lefs,REPO/'src/fin/test/fill_met2_only.json','met2'),
        ('tpu_1x1', WORKSPACE/'asap7/tpu/tpu.orfs.out.def',[orfs/'asap7_tech_1x_201209.lef',orfs/'asap7sc7p5t_28_R_1x_220121a.lef'],WORKSPACE/'scripts/fill_asap7_m2_thread_sweep.json','M2')]
    if args.cases:
        cases = [case for case in cases if case[0] in args.cases]
    for _, source, lefs, rules, _ in cases:
        for path in [source,rules,*lefs,args.openroad]:
            if not path.is_file():parser.error('Missing file: '+str(path))
    print('Results: '+str(output),flush=True)
    if args.dry_run:
        for name,*_ in cases:print(name+': density, min_amount, lp_minvar')
        return 0
    output.mkdir(parents=True,exist_ok=False)
    metadata = dict(binary=str(args.openroad.resolve()),binary_sha256=digest(args.openroad),cases=[],
                    evaluation=dict(window_um=50,step_um=12.5,min_window_density=0.3,max_window_density=0.6),
                    source_sha256={str(p.relative_to(REPO)):digest(p) for p in [
                        REPO/'src/fin/src/FixedDissectionLp.h', REPO/'src/fin/src/FillLpSolver.cpp',
                        REPO/'src/fin/src/LipLpFill.cpp', REPO/'src/fin/src/LPFill.cpp']})
    for source in [Path(__file__), REPO/'src/fin/test/script/compare_m2_fill_quality.tcl']:
        (output/source.name).write_bytes(source.read_bytes())
    for name,source,lefs,rules,layer in cases:
        snapshot = output/(name+'_rules.json')
        snapshot.write_bytes(rules.read_bytes())
        metadata['cases'].append(dict(name=name,input=str(source),input_sha256=digest(source),rules=snapshot.name,rules_sha256=digest(snapshot),lefs={str(p):digest(p) for p in lefs}))
    (output/'experiment.json').write_text(json.dumps(metadata,indent=2)+'\n')
    for name,source,lefs,_,layer in cases:
        for method in ['density','min_amount','lp_minvar']:
            run = output/name/method;run.mkdir(parents=True)
            env = {k:v for k,v in os.environ.items() if not k.startswith('FIN_')}
            env.update(QUALITY_INPUT=str(source),QUALITY_LEFS=' '.join(map(str,lefs)),QUALITY_RULES=str(output/(name+'_rules.json')),QUALITY_OUTPUT=str(run),QUALITY_METHOD=method,QUALITY_LAYER=layer,FIN_LP_SOLVER_LOG='0',FIN_LP_DUAL_TOLERANCE='0',FIN_TRACE_CANDIDATES='0',OMP_DYNAMIC='FALSE',OMP_NUM_THREADS='1')
            cmd=[str(args.openroad.resolve()),'-no_init','-no_splash','-exit','-threads','1',str(REPO/'src/fin/test/script/compare_m2_fill_quality.tcl')]
            (run/'invocation.json').write_text(json.dumps(dict(case=name,method=method,layer=layer,command=cmd,environment={k:v for k,v in env.items() if k.startswith(('QUALITY_','FIN_'))}),indent=2)+'\n')
            start=time.monotonic();print('Running '+name+' / '+method,flush=True)
            with (run/'openroad.log').open('w') as log:
                try:
                    code=subprocess.call(cmd,env=env,cwd=str(REPO),stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
                    timed_out=False
                except subprocess.TimeoutExpired:
                    code=124;timed_out=True
            (run/'process.json').write_text(json.dumps(dict(returncode=code,timed_out=timed_out,wall_seconds=time.monotonic()-start,status='ok' if code==0 else 'failed'),indent=2)+'\n')
            print('Finished '+name+' / '+method+' returncode='+str(code),flush=True)
    if digest(args.openroad)!=metadata['binary_sha256']:
        raise RuntimeError('OpenROAD binary changed during comparison')
    summarize(output)
    return 0


if __name__ == '__main__':
    sys.exit(main())

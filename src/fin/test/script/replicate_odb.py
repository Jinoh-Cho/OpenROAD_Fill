#!/usr/bin/env python3
"""Read a flat routed DEF/ODB once, replicate inside OpenDB, and save an ODB.

Requires the Tcl-enabled OpenROAD executable, not its Python module. Defaults to
the ASAP7 LEFs used by the TPU experiments. Does not generate an expanded DEF.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
WORKSPACE = REPO.parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--nx', required=True, type=int)
    p.add_argument('--ny', required=True, type=int)
    p.add_argument('--gap-x', type=int, default=0, help='Gap in database units')
    p.add_argument('--gap-y', type=int, default=0, help='Gap in database units')
    p.add_argument('--share-power', action='store_true')
    p.add_argument('--lefs', nargs='+', type=Path)
    p.add_argument('--openroad', type=Path,
                   default=Path(os.environ.get('OPENROAD_BIN', str(REPO / 'bazel-bin/openroad'))))
    p.add_argument('--write-def', type=Path, help='Optional small-case validation DEF')
    p.add_argument('--threads', type=int, default=1,
                   help='OpenROAD threads; replication itself is serial')
    args = p.parse_args()
    if min(args.nx, args.ny, args.threads) < 1 or min(args.gap_x, args.gap_y) < 0:
        p.error('Counts must be positive and gaps nonnegative')
    args.input = args.input.expanduser().resolve()
    args.output = args.output.expanduser().absolute()
    args.openroad = args.openroad.expanduser().resolve()
    if not args.input.is_file():
        p.error('Input does not exist: ' + str(args.input))
    if args.output.suffix != '.odb':
        p.error('Output must end in .odb')
    if not args.openroad.is_file() or not os.access(str(args.openroad), os.X_OK):
        p.error('Executable not found: ' + str(args.openroad))
    targets = [args.output, Path(str(args.output) + '.json')]
    if args.write_def:
        args.write_def = args.write_def.expanduser().absolute()
        targets.append(args.write_def)
    for target in targets:
        if target.exists() or target.is_symlink():
            p.error('Will not overwrite: ' + str(target))
        target.parent.mkdir(parents=True, exist_ok=True)
    if len(set(str(t.resolve()) for t in targets)) != len(targets):
        p.error('Output paths must be distinct')
    lefs = args.lefs
    if lefs is None:
        platform = Path(os.environ.get('ORFS_ROOT', str(Path.home() / 'OpenROAD-flow-scripts'))) / 'flow/platforms/asap7/lef'
        lefs = [platform / 'asap7_tech_1x_201209.lef',
                platform / 'asap7sc7p5t_28_R_1x_220121a.lef']
    if args.input.suffix != '.odb':
        for lef in lefs:
            if not lef.is_file():
                p.error('Missing LEF: ' + str(lef))
    started = time.monotonic()
    # Staging on the output filesystem permits atomic no-overwrite publication.
    with tempfile.TemporaryDirectory(prefix='.replicate_odb_', dir=str(args.output.parent)) as stage:
        stage = Path(stage)
        env = os.environ.copy()
        for key, value in {'INPUT': args.input, 'OUTPUT': stage / 'result.odb',
                           'STATS': stage / 'stats.txt', 'NX': args.nx, 'NY': args.ny,
                           'GAP_X': args.gap_x, 'GAP_Y': args.gap_y,
                           'SHARE': int(args.share_power),
                           'DEF': stage / 'result.def' if args.write_def else '',
                           'LEF_COUNT': len(lefs)}.items():
            env['ODB_REPLICATE_' + key] = str(value)
        for i, lef in enumerate(lefs):
            env['ODB_REPLICATE_LEF_' + str(i)] = str(lef.expanduser().resolve())
        command = [str(args.openroad), '-no_init', '-no_splash', '-exit',
                   '-threads', str(args.threads), str(HERE / 'replicate_odb.tcl')]
        print('+ ' + ' '.join(command), flush=True)
        subprocess.run(command, env=env, check=True)
        stats = dict(line.split('=', 1) for line in (stage / 'stats.txt').read_text().splitlines())
        metadata = {'input': str(args.input), 'output': str(args.output),
                    'nx': args.nx, 'ny': args.ny, 'share_power': args.share_power,
                    'gap_x': args.gap_x, 'gap_y': args.gap_y,
                    'openroad': str(args.openroad), 'stats': stats,
                    'seconds_before_publication': time.monotonic() - started,
                    'bytes': (stage / 'result.odb').stat().st_size,
                    'tile_0_names': 'Original names retained; other tiles use tCOLUMN_ROW__'}
        (stage / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        # Output file is published last; a concurrent preexisting file is never replaced.
        if args.write_def:
            # DEF validation output may be on another filesystem.
            temporary_def = None
            try:
                with tempfile.NamedTemporaryFile(dir=str(args.write_def.parent),
                                                 prefix='.replicate_odb_def_', delete=False) as dst:
                    temporary_def = Path(dst.name)
                    with (stage / 'result.def').open('rb') as src:
                        shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                os.link(str(temporary_def), str(args.write_def))
            finally:
                if temporary_def is not None:
                    temporary_def.unlink()
        os.link(str(stage / 'metadata.json'), str(targets[1]))
        os.link(str(stage / 'result.odb'), str(args.output))
    print('Created %s (%.3f GiB); total %.3fs' %
          (args.output, args.output.stat().st_size / 2**30, time.monotonic() - started), flush=True)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit(130)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('error: ' + str(error))

#!/usr/bin/env python3
"""Replicate a flat routed DEF/ODB using native C++ loops; save uncompressed ODB.

Build the small helper once with --build. Existing Tcl and DEF replicators are
unchanged. The native replication loop is serial and avoids Tcl/SWIG calls.
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
TARGET = '//src/fin/test/script/odb_native:replicate_odb'
DEFAULT_BACKEND = REPO / 'bazel-bin/src/fin/test/script/odb_native/replicate_odb'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--nx', type=int, required=True)
    p.add_argument('--ny', type=int, required=True)
    p.add_argument('--gap-x', type=int, default=0)
    p.add_argument('--gap-y', type=int, default=0)
    p.add_argument('--share-power', action='store_true')
    p.add_argument('--lefs', nargs='+', type=Path)
    p.add_argument('--openroad', type=Path, default=Path(os.environ.get('OPENROAD_BIN', str(REPO / 'bazel-bin/openroad'))))
    p.add_argument('--backend', type=Path, default=Path(os.environ.get('ODB_REPLICATE_BACKEND', str(DEFAULT_BACKEND))))
    p.add_argument('--build', action='store_true', help='Build/update the native helper before running')
    p.add_argument('--jobs', type=int, default=16)
    p.add_argument('--write-def', type=Path, help='Optional small-case validation export')
    args = p.parse_args()
    if min(args.nx, args.ny, args.jobs) < 1 or min(args.gap_x, args.gap_y) < 0:
        p.error('Counts must be positive and gaps nonnegative')
    if max(args.nx, args.ny, args.gap_x, args.gap_y) > 2147483647:
        p.error('Counts/gaps must fit signed 32-bit integers')
    args.input = args.input.expanduser().resolve()
    args.output = args.output.expanduser().absolute()
    args.openroad = args.openroad.expanduser().resolve()
    args.backend = args.backend.expanduser().absolute()
    if not args.input.is_file():
        p.error('Missing input: ' + str(args.input))
    if args.output.suffix != '.odb':
        p.error('Output must end in .odb')
    targets = [args.output, Path(str(args.output) + '.json')]
    if args.write_def:
        args.write_def = args.write_def.expanduser().absolute()
        targets.append(args.write_def)
    for target in targets:
        if target.exists() or target.is_symlink():
            p.error('Will not overwrite: ' + str(target))
        target.parent.mkdir(parents=True, exist_ok=True)
    if len(set(str(t.resolve()) for t in targets)) != len(targets):
        p.error('Output paths must differ')
    if args.build:
        subprocess.run(['bazelisk', 'build', TARGET, '-c', 'opt', '--jobs=' + str(args.jobs)],
                       cwd=str(REPO), check=True)
    if not args.backend.is_file() or not os.access(str(args.backend), os.X_OK):
        p.error('Native backend missing. Add --build, or run: bazelisk build ' + TARGET)
    lefs = args.lefs
    if lefs is None:
        platform = Path(os.environ.get('ORFS_ROOT', str(Path.home() / 'OpenROAD-flow-scripts'))) / 'flow/platforms/asap7/lef'
        lefs = [platform / 'asap7_tech_1x_201209.lef', platform / 'asap7sc7p5t_28_R_1x_220121a.lef']
    if args.input.suffix != '.odb':
        for lef in lefs:
            if not lef.is_file():
                p.error('Missing LEF: ' + str(lef))
    if args.input.suffix != '.odb' or args.write_def:
        if not args.openroad.is_file() or not os.access(str(args.openroad), os.X_OK):
            p.error('OpenROAD executable missing: ' + str(args.openroad))
    env = os.environ.copy()
    env['ODB_NATIVE_LEF_COUNT'] = str(len(lefs))
    for i, lef in enumerate(lefs):
        env['ODB_NATIVE_LEF_' + str(i)] = str(lef.expanduser().resolve())

    def bridge(mode, source, target):
        env.update(ODB_NATIVE_MODE=mode, ODB_NATIVE_INPUT=str(source), ODB_NATIVE_OUTPUT=str(target))
        subprocess.run([str(args.openroad), '-no_init', '-no_splash', '-exit',
                        str(HERE / 'replicate_odb_native_prepare.tcl')], env=env, check=True)

    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='.replicate_odb_native_', dir=str(args.output.parent)) as directory:
        stage = Path(directory)
        source = args.input
        prepare = time.monotonic()
        if source.suffix != '.odb':
            source = stage / 'source.odb'
            print('Preparing the original ODB once (no expanded DEF)', flush=True)
            bridge('prepare', args.input, source)
        prepare_seconds = time.monotonic() - prepare
        result = stage / 'result.odb'
        command = [str(args.backend), str(source), str(result), str(args.nx), str(args.ny),
                   str(args.gap_x), str(args.gap_y), str(int(args.share_power)), str(stage / 'stats.txt')]
        print('+ ' + ' '.join(command), flush=True)
        subprocess.run(command, check=True)
        stats = dict(line.split('=', 1) for line in (stage / 'stats.txt').read_text().splitlines())
        metadata = {'input': str(args.input), 'output': str(args.output),
                    'nx': args.nx, 'ny': args.ny, 'gap_x': args.gap_x, 'gap_y': args.gap_y,
                    'share_power': args.share_power, 'backend': str(args.backend.resolve()),
                    'source_prepare_seconds': prepare_seconds, 'stats': stats,
                    'bytes': result.stat().st_size,
                    'tile_0_names': 'Original names retained; other tiles use tCOLUMN_ROW__'}
        if args.write_def:
            bridge('export', result, stage / 'result.def')
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=str(args.write_def.parent),
                                                 prefix='.replicate_odb_export_', delete=False) as dst:
                    temporary = Path(dst.name)
                    with (stage / 'result.def').open('rb') as src:
                        shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                os.link(str(temporary), str(args.write_def))
            finally:
                if temporary:
                    temporary.unlink()
        metadata['seconds_before_publication'] = time.monotonic() - started
        (stage / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        os.link(str(stage / 'metadata.json'), str(targets[1]))
        os.link(str(result), str(args.output))
    print('Created %s (%.3f GiB), total %.3fs' %
          (args.output, args.output.stat().st_size / 2**30, time.monotonic() - started), flush=True)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit(130)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('error: ' + str(error))

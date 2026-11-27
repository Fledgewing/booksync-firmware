#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""BookSync F03: retained reader smoke with an isolated, synthetic legacy SD tree.

Uses the existing production simulator scenarios. Does not invoke import, sync,
NVS provisioning, a device, or serial I/O. Each invocation runs once; no retries.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('reader_smoke', ROOT / 'scripts/run_simulator_smoke_test.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)
original_prepare = smoke.prepare_fs
original_run = smoke.subprocess.run
protected = {}


def seed(temp_root, book):
    destination = temp_root / 'fs_'
    source = ROOT / 'test/fixtures/legacy/sd'
    # The retained smoke suite first asserts that native stats are absent.
    # Native formats are exercised by the host production-loader tests; seed
    # private legacy records here without violating that cold-start contract.
    for original in source.rglob('*'):
        if not original.is_file():
            continue
        relative = original.relative_to(source)
        family = relative.parts[1]
        if not (family.startswith(('bookorbit', 'book_', 'orbify')) or family == 'orbismart'):
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        protected[target] = hashlib.sha256(target.read_bytes()).digest()
    layout = json.loads((ROOT / 'test/fixtures/legacy/sd-layout.json').read_text())
    for directory in layout['required_directories']:
        (destination / directory.lstrip('/')).mkdir(parents=True, exist_ok=True)
    return original_prepare(temp_root, book)


def run_and_verify(command, **kwargs):
    result = original_run(command, **kwargs)
    if Path(command[0]).name == 'program':
        for path, digest in protected.items():
            if not path.is_file() or hashlib.sha256(path.read_bytes()).digest() != digest:
                raise AssertionError('Legacy source changed: ' + str(path.relative_to(Path(kwargs['cwd'])/'fs_')))
        print(f'Legacy opaque sources preserved: {len(protected)}', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--env', required=True, choices=('x4-pro-simulator','x4-classic-simulator','simulator-X3'))
    parser.add_argument('--scenario', choices=('reading','sleep','bookmarks','typography'), default='reading')
    parser.add_argument('--no-build', action='store_true')
    options = parser.parse_args()
    arguments = ['--env', options.env]
    if options.no_build:
        arguments.append('--no-build')
    arguments += {'reading': [], 'sleep': ['--custom-sleep-images'],
                  'bookmarks': ['--bookmarks'], 'typography': ['--ruled-lines']}[options.scenario]
    smoke.prepare_fs = seed
    smoke.subprocess.run = run_and_verify
    sys.argv = [str(ROOT / 'scripts/run_simulator_smoke_test.py')] + arguments
    return smoke.main()


if __name__ == '__main__':
    raise SystemExit(main())

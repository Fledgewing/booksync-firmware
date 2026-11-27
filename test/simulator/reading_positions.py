#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""F27a two real-process boots retain F03 source bytes and import proofs."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unknown profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
fixtures = root / 'test/fixtures/legacy'
with tempfile.TemporaryDirectory(prefix='booksync-positions-') as directory:
    sd = Path(directory) / 'fs_'
    shutil.copytree(fixtures / 'sd', sd)
    (sd / 'legacy').mkdir()
    for name, origin in {
        'progress.bin': 'sd/.crosspoint/epub_305419896/progress.bin',
        'progress.bin.bak': 'sd/.crosspoint/epub_305419896/progress.bin.bak',
        'torn.bin': 'variants/progress/torn.bin',
        'future.bin': 'variants/unknown/future.bin',
    }.items():
        shutil.copy2(fixtures / origin, sd / 'legacy' / name)
    def inventory():
        return {str(p.relative_to(sd)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sd.rglob('*') if p.is_file() and
                (str(p.relative_to(sd)).startswith(('.crosspoint/', 'legacy/')))}
    before = inventory()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_READING_POSITIONS_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    for boot in range(2):
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=120)
        log = result.stdout + result.stderr
        (out / f'reading-positions-{boot}.log').write_text(log)
        if result.returncode or 'BookSync reading positions scenario PASS' not in log:
            raise SystemExit(f'F27a {profile} boot {boot} failed')
        if inventory() != before:
            raise SystemExit('synthetic upgrade inventory changed')
(out / 'reading-positions.json').write_text(json.dumps({
    'result': 'PASS', 'boots': 2, 'profile': profile, 'source_files': len(before),
    'runtime_memory': '8192-byte reusable workspace plus journal reference; no dynamic payload allocations; HAL handle allocation remains. Host heap/stack do not qualify device acceptance.',
    'activation': 'not installed; F29 owns cutover',
}, indent=2) + '\n')
print(f'BookSync F27a {profile}: 2 boots PASS; {len(before)} source files unchanged')

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""F08: real native reader reflows and reopens F03's raw source anchor, offline."""
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
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
fixture = root / 'test/fixtures/legacy/sd/.crosspoint/epub_305419896/progress.bin'
with tempfile.TemporaryDirectory(prefix='booksync-locator-') as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'Books').mkdir(parents=True)
    shutil.copy2(root / 'test/epubs/test_reader_rendering_matrix.epub', sd / 'Books/Synthetic.epub')
    shutil.copy2(fixture, sd / 'f08-anchor.bin')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_LOCATOR_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'locator.log').write_text(log)
    if result.returncode or 'BookSync locator scenario PASS' not in log:
        raise SystemExit(f'F08 {profile} failed (exit {result.returncode}); see reports/{profile}/locator.log')
    if (sd / 'f08-anchor.bin').read_bytes() != fixture.read_bytes():
        raise SystemExit('F03 source fixture changed')
(out / 'locator.json').write_text(json.dumps({'profile': profile, 'result': 'PASS',
    'scenarios': ['account isolation and ambiguous alias', 'exact revision at reused path',
                  'F03 raw anchor loaded', 'native rotation reflow', 'native close and reopen'],
    'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
    'runtime_memory': 'Production identity/locator routines allocate zero bytes; borrowed views. Native host heap/stack do not qualify ESP32 memory.'}, indent=2) + '\n')
print(f'BookSync F08 {profile}: 5 identity/anchor checks PASS')

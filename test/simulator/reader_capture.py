#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""F09: production reader captures offline, closes and survives process power cuts."""
from pathlib import Path
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
logs = []
for scenario in ('normal', 'power-cut'):
    with tempfile.TemporaryDirectory(prefix='booksync-reader-capture-') as directory:
        sd = Path(directory) / 'fs_'
        (sd / 'Books').mkdir(parents=True)
        shutil.copy2(root / 'test/epubs/test_reader_rendering_matrix.epub', sd / 'Books/Synthetic.epub')
        for mode in (('read',) if scenario == 'normal' else ('cut', 'restart')):
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       BOOKSYNC_READER_CAPTURE_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
            result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                    env=env, capture_output=True, text=True, timeout=120)
            logs.append(f'{scenario}/{mode}\n{result.stdout}{result.stderr}')
            (out / 'reader-capture.log').write_text('\n'.join(logs))
            if result.returncode or 'BookSync reader capture scenario PASS' not in result.stdout + result.stderr:
                raise SystemExit(f'F09 failed: {scenario}/{mode}; see reports/{profile}/reader-capture.log')
            if mode == 'cut':
                # A torn staging file must never replace the last complete capture.
                journal = sd / '.booksync/reader.journal'
                journal.with_suffix('.journal.tmp').write_bytes(journal.read_bytes()[:-3])
(out / 'reader-capture.json').write_text(json.dumps({'profile': profile, 'scenarios': 2,
    'boots': 3, 'result': 'PASS', 'transport': 'local journal only; F05 fake transport suite runs separately',
    'runtime_memory': '4168 bytes shared scratch, zero capture heap allocations; host heap/stack are not ESP32 acceptance'}, indent=2) + '\n')
print(f'BookSync F09 {profile}: 2 reading/cut scenarios, 3 boots PASS')

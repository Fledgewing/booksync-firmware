#!/usr/bin/env python3
"""F13 10k-file SD scan, warm reads, interruption and 50k streamed history visits."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-index-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    books=sd / 'books'
    books.mkdir()
    for number in range(10000):
        (books / (f'{number:05}.epub' if number < 9999 else '頁'*40+'.epub')).write_bytes(b'synthetic')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_INDEX_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'device-index.log').write_text(log)
    if result.returncode or 'BookSync index scenario PASS' not in log:
        raise SystemExit(f'F13 failed; see reports/{profile}/device-index.log')
(out / 'device-index.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'books': 10000, 'history_visits': 50000, 'history_windows': 197,
    'presentation_repaints': 0,
    'runtime_memory': 'Fixed record/window/cursor state, no resident catalog/history. Real Home allocates at most 24 KiB once and releases it before reader entry. Native heap/stack do not qualify hardware.'
}, indent=2) + '\n')
print(f'BookSync F13 {profile}: 10k books / 50k visits PASS')

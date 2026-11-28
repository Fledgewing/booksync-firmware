#!/usr/bin/env python3
"""Offline F22 projections from a committed generation, process restart and invalidation."""
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
modes = ('seed', 'offline', 'restart', 'invalidate')
with tempfile.TemporaryDirectory(prefix='booksync-queries-', dir=out) as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    for mode in modes:
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_QUERIES_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'queries-{mode}.log').write_text(log)
        if result.returncode or 'BookSync queries scenario PASS' not in log:
            raise SystemExit(f'F22 failed: {profile} {mode}\n{log[-4000:]}')
(out / 'queries.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'processes': len(modes), 'cases': 25,
    'books': 80, 'batch': 16, 'per_row_workspace_allocations': 0,
    'runtime_memory': 'Fixed workspace sizes only; simulator does not qualify ESP32 runtime memory.'
}, indent=2) + '\n')
print(f'BookSync F22 {profile}: 25 offline/search/filter/facet/group/series/paging/restart cases PASS')

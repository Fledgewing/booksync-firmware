#!/usr/bin/env python3
"""F21 real simulator process cuts while refreshing, scoped restart and Home retirement."""
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
modes = ('seed', 'recover', 'cut-begin', 'recover', 'cut-stream', 'recover', 'cancel', 'recover', 'refresh', 'recover-new')
with tempfile.TemporaryDirectory(prefix='booksync-catalog-jobs-', dir=out) as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    for i, mode in enumerate(modes):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_CATALOG_JOBS_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'catalog-jobs-{i}-{mode}.log').write_text(log)
        cut = mode.startswith('cut-')
        if result.returncode != (73 if cut else 0) or ('intentional power cut' if cut else 'BookSync catalog jobs scenario PASS') not in log:
            raise SystemExit(f'F21 failed: {profile} {mode}\n{log[-4000:]}')
(out / 'catalog-jobs.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'scenarios': len(modes), 'power_cuts': 2,
    'books': 80, 'batch': 16, 'per_row_allocations': 0,
    'runtime_memory': 'Fixed workspace sizes in logs. Simulator values do not qualify ESP32 heap, largest block or stack high-water.'
}, indent=2) + '\n')
print(f'BookSync F21 {profile}: {len(modes)} refresh/cut/restart scenarios PASS')

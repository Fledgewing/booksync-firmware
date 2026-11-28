#!/usr/bin/env python3
"""F36c exact-choice queues through the production single writer; isolated synthetic SD."""
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
cases = ('normal', 'cancel', 'unsupported', 'offline', 'failure', 'stale')
for mode in cases:
    with tempfile.TemporaryDirectory(prefix='series-queue-', dir=out) as directory:
        sd = Path(directory) / 'fs_'
        sd.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_SERIES_QUEUE_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'series-queue-{mode}.log').write_text(log)
        if result.returncode or 'BookSync Series queue PASS' not in log:
            raise SystemExit(f'F36c queue failed: {profile} {mode}\n{log[-4000:]}')
(out / 'series-queue.json').write_text(json.dumps({
    'profile': profile, 'cases': len(cases), 'result': 'PASS', 'max_queues': 4, 'max_selection': 10, 'single_writer': True,
    'hardware_memory': 'Fixed workspace; simulator is not embedded heap/stack acceptance.'
}, indent=2) + '\n')
print(f'BookSync F36c {profile}: {len(cases)} Series queue scenarios PASS')

with tempfile.TemporaryDirectory(prefix='series-recovery-', dir=out) as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    for mode, expected in [('cut',73),('recover',0),('resume',0)]:
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_SERIES_QUEUE_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log=result.stdout+result.stderr
        (out / f'series-queue-{mode}.log').write_text(log)
        if result.returncode != expected or (mode!='cut' and 'BookSync Series queue PASS' not in log):
            raise SystemExit(f'F36c recovery failed: {profile} {mode}\n{log[-4000:]}')
print(f'BookSync F36c {profile}: queue cut/recover/resume PASS')

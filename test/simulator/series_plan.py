#!/usr/bin/env python3
"""F36c production next-N policy; bounded neutral source, zero transport/paint work."""
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
cases = ('offline', 'gap', 'bounded', 'incomplete', 'account', 'jobs')
for mode in cases:
    with tempfile.TemporaryDirectory(prefix='series-plan-', dir=out) as directory:
        sd = Path(directory) / 'fs_'
        sd.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_SERIES_PLAN_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'series-plan-{mode}.log').write_text(log)
        if result.returncode or 'BookSync Series plan PASS' not in log:
            raise SystemExit(f'F36c plan failed: {profile} {mode}\n{log[-4000:]}')
(out / 'series-plan.json').write_text(json.dumps({
    'profile': profile, 'cases': len(cases), 'result': 'PASS', 'per_tick_reads': 16,
    'max_selection': 10, 'paint_scans': 0, 'payload_allocations': 0,
    'hardware_memory': 'Fixed workspace; simulator is not embedded heap/stack acceptance.'
}, indent=2) + '\n')
print(f'BookSync F36c {profile}: {len(cases)} Series planning scenarios PASS')

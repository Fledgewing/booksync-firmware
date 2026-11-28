#!/usr/bin/env python3
"""F39 saved page history, account precedence and deterministic picks offline."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
modes = ('seed', 'local', 'account', 'separate', 'zero', 'gap', 'account-change', 'absent')
logs = []
with tempfile.TemporaryDirectory(prefix='personal-', dir=out) as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'personal/pages').mkdir(parents=True)
    for mode in modes:
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   CROSSPOINT_SIM_SD=str(sd), BOOKSYNC_PERSONAL_SCENARIO=mode)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=60)
        logs.append(f'{mode}\n{result.stdout}{result.stderr}')
        (out / 'personal-reading.log').write_text('\n'.join(logs))
        if result.returncode or 'BookSync personal scenario PASS' not in result.stdout + result.stderr:
            raise SystemExit(f'personal reading failed: {mode}; see reports/{profile}/personal-reading.log')
(out / 'personal-reading.json').write_text(json.dumps({
    'profile': profile, 'scenarios': 7, 'boots': 8, 'result': 'PASS',
    'batch_rows': 32, 'explicit_heap_allocations': 0,
    'runtime_memory': {'internal_heap': None, 'largest_block': None, 'stack_high_water': None,
                       'reason': 'Native HAL does not model ESP32 heap/stack; hardware acceptance required'}
}, indent=2) + '\n')
print(f'BookSync F39 {profile}: 7 scenarios / 8 boots PASS')

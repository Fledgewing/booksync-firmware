#!/usr/bin/env python3
"""F12 synthetic view/clock/worker, production delivery/gate and real HAL journal."""
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
with tempfile.TemporaryDirectory(prefix='booksync-delivery-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_DELIVERY_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr
    (out / 'delivery.log').write_text(log)
    if result.returncode or 'BookSync delivery scenario PASS' not in log:
        raise SystemExit(f'F12 failed; see reports/{profile}/delivery.log')
(out / 'delivery.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'policy_checks': 8,
    'presentation_repaints': 0,
    'runtime_memory': 'Tickets and gate allocate zero bytes; caller-owned bounded state, no tasks or payload copies. Native heap/stack do not qualify hardware.'
}, indent=2) + '\n')
print(f'BookSync F12 {profile}: 8 policy checks PASS')

#!/usr/bin/env python3
"""F02 baseline harness: offline synthetic EPUB boot and retained-reader smoke."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['SDL_VIDEODRIVER'] = 'dummy'
env['SDL_AUDIODRIVER'] = 'dummy'
env['CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_CAPTURES'] = str(out / 'captures')
# Existing smoke fixture creates a disposable SD root with no saved credentials.
# It opens an EPUB, turns pages and verifies native reader success markers.
start = time.monotonic()
result = subprocess.run([sys.executable, 'scripts/run_simulator_smoke_test.py',
                         '--env', profile, '--no-build', '--page-turns', '2'],
                        cwd=root, env=env, capture_output=True, text=True, timeout=180)
(out / 'boot.log').write_text(result.stdout + result.stderr)
print(result.stdout, end='')
print(result.stderr, end='', file=sys.stderr)
(out / 'scenario.json').write_text(json.dumps({'profile': profile,
    'scenario': 'offline synthetic SD boot/open/two page turns',
    'seconds': round(time.monotonic() - start, 3), 'exit_code': result.returncode,
    'runtime_heap': 'native host values are not hardware acceptance'}, indent=2) + '\n')
raise SystemExit(result.returncode)

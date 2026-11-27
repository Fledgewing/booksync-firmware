#!/usr/bin/env python3
"""F02 baseline harness: offline synthetic EPUB boot and retained-reader smoke."""
from pathlib import Path
import hashlib
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
env['BOOKSYNC_CI_CAPTURE'] = '1'
env['SDL_VIDEODRIVER'] = 'dummy'
env['SDL_AUDIODRIVER'] = 'dummy'
env['CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_CAPTURES'] = str(out / 'captures')
# Existing smoke fixture creates a disposable SD root with no saved credentials.
# It opens an EPUB, turns pages and verifies native reader success markers.
config = json.loads((root / 'tools/ci/simulator.json').read_text())
simulator = root / '.pio/libdeps' / profile / 'simulator'
actual_pin = subprocess.check_output(['git', '-C', str(simulator), 'rev-parse', 'HEAD'], text=True).strip()
if actual_pin != config['commit']:
    raise SystemExit('simulator dependency differs from immutable pin')
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
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'simulator_commit': actual_pin,
    'program_sha256': hashlib.sha256((root / '.pio/build' / profile / 'program').read_bytes()).hexdigest(),
    'runtime_heap': 'native host values are not hardware acceptance'}, indent=2) + '\n')
if result.returncode == 0 and not list((out / 'captures').glob('*.pgm')):
    raise SystemExit('simulator did not retain baseline frames')
raise SystemExit(result.returncode)

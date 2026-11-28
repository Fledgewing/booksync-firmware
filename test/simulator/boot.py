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
subprocess.run([sys.executable, 'tools/ci/retired_writers.py', str(root / '.pio/build' / profile / 'program')], cwd=root, check=True)
subprocess.run([sys.executable, 'test/simulator/admission.py', profile], cwd=root, check=True)
# F10 probes run exactly once; a failing synthetic reconnect stops this profile.
subprocess.run([sys.executable, 'test/simulator/tailnet.py', profile], cwd=root, check=True)
subprocess.run([sys.executable, 'test/simulator/identity_import.py', profile], cwd=root, check=True)
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
if result.returncode:
    raise SystemExit(result.returncode)

# F03 retains the cold-start contract above, then checks private legacy sidecars
# alongside the reader's own fixtures. Each scenario runs once; failures stop CI.
for scenario in ('reading', 'sleep', 'bookmarks', 'typography'):
    started = time.monotonic()
    case = subprocess.run([sys.executable, 'test/simulator/reader_baseline.py',
                           '--env', profile, '--scenario', scenario, '--no-build'],
                          cwd=root, env=env, capture_output=True, text=True, timeout=180)
    (out / f'legacy-{scenario}.log').write_text(case.stdout + case.stderr)
    print(case.stdout, end='')
    print(case.stderr, end='', file=sys.stderr)
    (out / f'legacy-{scenario}.json').write_text(json.dumps({
        'profile': profile, 'scenario': scenario, 'exit_code': case.returncode,
        'seconds': round(time.monotonic() - started, 3),
        'simulator_commit': actual_pin}, indent=2) + '\n')
    if case.returncode:
        raise SystemExit(case.returncode)
subprocess.run([sys.executable, 'test/simulator/file_publish.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/native_writers_retired.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/lifecycle_journal.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/locator.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/home.py', profile], cwd=root, env=env, check=True)

subprocess.run([sys.executable, "test/simulator/reader_capture.py", profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/delivery.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/device_index.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/size_gate.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, "test/simulator/sync_owner.py", profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/layout.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/account.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/update.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/catalog_jobs.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/download_jobs.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/queries.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/catalog.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/lifecycle_match.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/widgets.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/credentials.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/version.py', profile], cwd=root, env=env, check=True)

subprocess.run([sys.executable, "test/simulator/lifecycle_stats.py", profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/editor.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/geometry.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/pages.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/lifecycle_state.py', profile], cwd=root, env=env, check=True)

subprocess.run([sys.executable, 'test/simulator/activation.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/lifecycle_annotations.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/personal_reading.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/account_surface.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, "test/simulator/lifecycle_progress.py", profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/copy_jobs.py', profile], cwd=root, env=env, check=True)


subprocess.run([sys.executable, 'test/simulator/update_install.py', profile], cwd=root, env=env, check=True)
subprocess.run([sys.executable, 'test/simulator/upgrade_notice.py', profile], cwd=root, env=env, check=True)
raise SystemExit(0)

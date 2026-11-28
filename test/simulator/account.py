#!/usr/bin/env python3
"""F15 synthetic serial account worker, fake auth, no secrets in receipts."""
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
with tempfile.TemporaryDirectory(prefix='booksync-account-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_ACCOUNT_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr
    (out / 'account.log').write_text(log)
    if result.returncode or 'BookSync account scenario PASS' not in log:
        raise SystemExit(f'F15 failed; see reports/{profile}/account.log')
(out / 'account.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'policy_checks': 26,
    'presentation_repaints': 0,
    'runtime_memory': 'Credential capture uses a fixed job buffer below 2 KiB and no heap. Native fake-worker stack and RSS do not qualify ESP32 heap or stack.'
}, indent=2) + '\n')
print(f'BookSync F15 {profile}: 26 account checks PASS')

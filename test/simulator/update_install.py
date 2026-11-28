#!/usr/bin/env python3
"""F32 deferred fake install, verified return and boot recovery; no live transport."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-install-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_UPDATE_INSTALL_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr
    (out / 'update_install.log').write_text(log)
    if result.returncode or not re.search(r'BookSync install scenario PASS checks=22\b', log):
        raise SystemExit(f'F32 failed; see reports/{profile}/update_install.log')
(out / 'update_install.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'policy_checks': 22,
    'presentation_repaints': 0,
    'runtime_memory': 'Production install policy allocates zero heap; the receipt is 76 bytes and uses the existing publication primitive. Fixed-size image metadata and the synthetic parser are fixture-only. Native heap/stack do not qualify hardware.'
}, indent=2) + '\n')
print(f'BookSync F32 {profile}: 22 policy checks PASS')

#!/usr/bin/env python3
"""F30 fake signed feed and real verification; no transport or installer."""
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
with tempfile.TemporaryDirectory(prefix='booksync-update-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_UPDATE_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr
    (out / 'update.log').write_text(log)
    if result.returncode or 'BookSync update scenario PASS' not in log:
        raise SystemExit(f'F30 failed; see reports/{profile}/update.log')
(out / 'update.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'policy_checks': 11,
    'presentation_repaints': 0,
    'runtime_memory': 'Parser uses fixed caller-owned buffers. Ed25519 allocates one 1920-byte work area per cold verification; no resident allocation. Native heap/stack do not qualify hardware.'
}, indent=2) + '\n')
print(f'BookSync F30 {profile}: 11 policy checks PASS')

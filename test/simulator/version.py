#!/usr/bin/env python3
"""F31 synthetic old to bridge to unified install choice; no installer."""
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
with tempfile.TemporaryDirectory(prefix='booksync-version-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_VERSION_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=30)
    log = result.stdout + result.stderr
    (out / 'version.log').write_text(log)
    if result.returncode or not re.search(r'BookSync version scenario PASS checks=99\b', log):
        raise SystemExit(f'F31 failed; see reports/{profile}/version.log')
(out / 'version.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'policy_checks': 99,
    'presentation_repaints': 0,
    'runtime_memory': 'Production version/picker functions allocate zero heap and use bounded scalars. Synthetic feed strings and parser storage are fixture-only. Native heap/stack do not qualify hardware.'
}, indent=2) + '\n')
print(f'BookSync F31 {profile}: 99 policy checks PASS')

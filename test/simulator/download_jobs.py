#!/usr/bin/env python3
"""F23 bounded synthetic transfers with real SD publication and process restarts."""
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
groups = (('download',), ('retired',), ('digest',), ('edition',), ('space',),
          ('cancel', 'recover-canceled'), ('cut', 'resume'), ('cut-published', 'resume-published'))
boots = 0
for group in groups:
    with tempfile.TemporaryDirectory(prefix='booksync-download-', dir=out) as directory:
        sd = Path(directory) / 'fs_'
        sd.mkdir()
        for mode in group:
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       BOOKSYNC_DOWNLOAD_JOBS_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
            result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                    env=env, capture_output=True, text=True, timeout=60)
            log = result.stdout + result.stderr
            (out / f'download-{mode}.log').write_text(log)
            cut = mode.startswith('cut')
            if result.returncode != (73 if cut else 0) or ('intentional power cut' if cut else 'BookSync download scenario PASS') not in log:
                raise SystemExit(f'F23 failed: {profile} {mode}\n{log[-4000:]}')
            boots += 1
(out / 'download-jobs.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'boots': boots, 'power_cuts': 2,
    'payload_allocations': 0, 'chunk_bytes': 1024,
    'runtime_memory': 'Fixed workspace size logged. Native heap/stack values do not qualify hardware acceptance.'
}, indent=2) + '\n')
print(f'BookSync F23 {profile}: {boots} download/fault/restart/delivery scenarios PASS')

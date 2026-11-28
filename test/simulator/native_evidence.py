#!/usr/bin/env python3
"""F46b1 real SD capture followed by a separate-process restart on each profile."""
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
with tempfile.TemporaryDirectory(prefix='booksync-native-evidence-', dir=out) as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    for mode in ('capture', 'restart'):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   CROSSPOINT_SIM_SD=str(sd), BOOKSYNC_NATIVE_EVIDENCE_SCENARIO=mode)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        (out / f'native-evidence-{mode}.log').write_text(log)
        if result.returncode or 'BookSync native evidence scenario PASS' not in log:
            raise SystemExit(f'F46b1 failed: {profile} {mode}\n{log[-4000:]}')
(out / 'native-evidence.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'scenarios': 2,
    'storage': 'real simulator SD files, separate process restart', 'network': 'none'
}, indent=2) + '\n')
print(f'BookSync F46b1 {profile}: 2 native capture/restart scenarios PASS')

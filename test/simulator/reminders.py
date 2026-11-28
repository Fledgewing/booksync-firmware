#!/usr/bin/env python3
"""R1 production policy/store with real native HAL, synthetic storage/time only."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
with tempfile.TemporaryDirectory(prefix='booksync-reminders-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_REMINDERS_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                            cwd=directory, env=env, capture_output=True, text=True, timeout=60)
    out = root / 'reports' / profile
    out.mkdir(parents=True, exist_ok=True)
    (out / 'reminders.log').write_text(result.stdout + result.stderr)
    if result.returncode or 'BookSync reminders scenario PASS' not in result.stdout + result.stderr:
        raise SystemExit(f'R1 failed; see reports/{profile}/reminders.log')
    assert (sd / 'reminders').stat().st_size <= 8192
print(f'BookSync R1 {profile}: local save/claim/snooze/restart PASS')

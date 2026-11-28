#!/usr/bin/env python3
"""Exercise the Library device bridge and retained native controls in isolation."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'device-bridge'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='source-', dir=out) as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_DEVICE_BRIDGE_TEST='1', CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=60)
    log = result.stdout + result.stderr
    (out / 'device-bridge.log').write_text(log)
    if result.returncode or 'BookSync device bridge PASS:' not in log:
        raise SystemExit(f'{profile} device bridge failed; see {out}/device-bridge.log')
print(f'BookSync device bridge {profile}: native controls and Library isolation PASS')

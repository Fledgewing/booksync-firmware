#!/usr/bin/env python3
"""F40w Continue reading: main-size scenes and independent states."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'continue-widget'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-widgets-') as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', BOOKSYNC_APPROVED_COVERS=str(root/'test/fixtures/f40w-covers'), SDL_AUDIODRIVER='dummy',
               BOOKSYNC_CONTINUE_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'continue.log').write_text(log)
    if result.returncode or 'BookSync Continue PASS:' not in log:
        raise SystemExit(f'Continue {profile} failed; see {out}/continue.log')
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines()), 'Home used an unprepared font'
    for scene in ('known', 'empty', 'not-saved', 'off', 'unsupported', 'error', 'oversize', 'stale', 'long-title'):
        assert (out / f'{scene}.pgm').stat().st_size > 1000
print(f'BookSync Continue {profile}: nine scenes PASS')

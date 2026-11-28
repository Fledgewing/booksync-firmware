#!/usr/bin/env python3
"""Approved F40w compact hours/records states; X3 remains uninstalled."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'records-widgets'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-records-widgets-') as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_RECORDS_WIDGET_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'records-widgets.log').write_text(log)
    if result.returncode or 'BookSync F40w hours/records PASS:' not in log:
        raise SystemExit(f'F40w {profile} failed; see {out}/records-widgets.log')
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    for scene in ('known', 'empty', 'not-saved', 'off', 'unsupported', 'stale', 'oversize', 'error', 'known-zero', 'stale-empty', 'account-only', 'add-list-final'):
        assert (out / f'{scene}.pgm').stat().st_size > 1000
if profile != 'simulator-X3':
    # Approved eight-pixel record gutters keep complete labels apart.
    for scene in ('known', 'stale'):
        header, dimensions, maximum, pixels = (out / f'{scene}.pgm').read_bytes().split(b'\n', 3)
        assert header == b'P5' and dimensions == b'480 800' and maximum == b'255'
        for left in (106, 195, 284, 373):
            assert all(pixels[y*480+x] == 255 for y in range(240, 254)
                       for x in range(left, left+4)), 'Reading record labels invade a column gutter'
print(f'BookSync F40w {profile}: twelve states PASS')

#!/usr/bin/env python3
"""Approved F40w compact week/streak states; X3 remains uninstalled."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'stats-widgets'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-stats-widgets-') as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_STATS_WIDGET_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'stats-widgets.log').write_text(log)
    if result.returncode or 'BookSync F40w week/streak PASS:' not in log:
        raise SystemExit(f'F40w {profile} failed; see {out}/stats-widgets.log')
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    for scene in ('known', 'empty', 'not-saved', 'off', 'unsupported', 'stale', 'oversize', 'error', 'known-zero', 'stale-empty', 'best-unknown'):
        assert (out / f'{scene}.pgm').stat().st_size > 1000
    if profile != 'simulator-X3':
        # Best is a separate saved value, retained when a stale-age line is added.
        # Compare only its text row, excluding focus borders and the new age row.
        def best_row(name, top):
            header, dimensions, maximum, pixels = (out / f'{name}.pgm').read_bytes().split(b'\n', 3)
            assert header == b'P5' and dimensions == b'480 800' and maximum == b'255'
            return b''.join(pixels[y*480+255:y*480+438] for y in range(top, top + 13))
        known_best, stale_best = best_row('known', 159), best_row('stale', 145)
        assert b'\0' in known_best and known_best == stale_best, 'Stale lost the saved best-streak row'
print(f'BookSync F40w {profile}: eleven states PASS')

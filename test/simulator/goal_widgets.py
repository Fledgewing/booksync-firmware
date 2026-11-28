#!/usr/bin/env python3
"""Approved F40w compact goal/forecast states; X3 remains uninstalled."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'goal-widgets'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-goal-widgets-') as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_GOAL_WIDGET_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'goal-widgets.log').write_text(log)
    if result.returncode or 'BookSync F40w goal/forecast PASS:' not in log:
        raise SystemExit(f'F40w {profile} failed; see {out}/goal-widgets.log')
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    for scene in ('known', 'empty', 'not-saved', 'off', 'unsupported', 'stale', 'oversize', 'error', 'known-zero', 'stale-empty', 'wrong-book', 'add-list-final'):
        assert (out / f'{scene}.pgm').stat().st_size > 1000
print(f'BookSync F40w {profile}: twelve states PASS')

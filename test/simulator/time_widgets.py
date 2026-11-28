#!/usr/bin/env python3
"""Approved Clock/Moon states on X4; X3 proves the new widgets stay off."""
from pathlib import Path
import json
import os
import resource
import subprocess
import sys
import tempfile
import time
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'time-widgets'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-time-widgets-', dir=out) as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_TIME_WIDGET_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    started = time.monotonic()
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    elapsed = time.monotonic() - started
    log = result.stdout + result.stderr
    (out / 'time-widgets.log').write_text(log)
    if result.returncode or 'BookSync Clock/Moon PASS:' not in log:
        raise SystemExit(f'{profile} Clock/Moon failed; see {out}/time-widgets.log')
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    scenes = ('x3-off',) if profile == 'simulator-X3' else ('known', 'empty', 'not-saved', 'off', 'unsupported', 'oversize', 'unavailable', 'stale', 'small')
    for scene in scenes:
        assert (out / f'{scene}.pgm').stat().st_size > 1000, scene
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    (out / 'metrics.json').write_text(json.dumps({'elapsed_seconds': elapsed,
        'native_peak_rss_bytes': rss if sys.platform == 'darwin' else rss * 1024,
        'projection_requested_allocation_bytes': 0, 'esp32_internal_free_bytes': None,
        'esp32_largest_block_bytes': None, 'esp32_stack_high_water_bytes': None}, indent=2))
print(f'BookSync Clock/Moon {profile}: {len(scenes)} scenes PASS')

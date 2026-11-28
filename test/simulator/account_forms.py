#!/usr/bin/env python3
"""F37b approved account states: all profiles and closed-view paint guard."""
from pathlib import Path
import os
import json
import resource
import time
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile / 'account-forms'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-account-forms-') as temporary:
    sd = Path(temporary) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_ACCOUNT_FORMS=str(out), CROSSPOINT_SIM_SD=str(sd))
    started = time.monotonic()
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                            cwd=temporary, env=env, capture_output=True, text=True, timeout=90)
    (out / 'scenario.log').write_text(result.stdout + result.stderr)
    if result.returncode or 'BookSync F37b PASS' not in result.stdout + result.stderr:
        raise SystemExit(f'F37b failed; see reports/{profile}/account-forms/scenario.log')
    assert len(list(out.glob('*.pgm'))) == 14
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    (out / 'metrics.json').write_text(json.dumps({
        'profile': profile, 'seconds': round(time.monotonic() - started, 3),
        'native_peak_rss_bytes': rss if sys.platform == 'darwin' else rss * 1024,
        'internal_heap_bytes': None, 'largest_internal_block_bytes': None,
        'worker_stack_high_water_bytes': None,
        'account_view_bound_bytes': 368, 'controller_bound_bytes': 2048,
        'note': 'Native RSS includes simulator/SDL; ESP32 heap and stack are unavailable. One bounded foreground entry and QR workspace; no extra framebuffer.'
    }, indent=2) + '\n')
print(f'BookSync F37b {profile}: 14 captures, picker/entry/Check/Back/epoch/closed-form/paint PASS')

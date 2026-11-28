#!/usr/bin/env python3
"""F35b real HAL editor intents, restart and approved presentation captures."""
from pathlib import Path
import json
import re
import resource
import time
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile / 'geometry'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-editor-') as temporary:
    sd = Path(temporary) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_GEOMETRY_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(sd))
    started = time.monotonic()
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                            cwd=temporary, env=env, capture_output=True, text=True, timeout=90)
    (out / 'scenario.log').write_text(result.stdout + result.stderr)
    if result.returncode or 'BookSync F35b PASS' not in result.stdout + result.stderr:
        raise SystemExit(f'F35b failed; see reports/{profile}/geometry/scenario.log')
    log = result.stdout + result.stderr
    workspace = re.search(r'workspace=(\d+)', log)
    worker = re.search(r'Local worker bounded allocation: (\d+) bytes', log)
    assert workspace and worker
    peak_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    metrics = dict(profile=profile, seconds=round(time.monotonic() - started, 3),
                   peak_rss_bytes=int(peak_rss if sys.platform == 'darwin' else peak_rss * 1024),
                   editor_workspace_bytes=int(workspace[1]), worker_peak_requested_bytes=int(worker[1]),
                   internal_heap_bytes=None, largest_block_bytes=None, task_stack_high_water=None,
                   note='Native RSS includes the simulator and fixture. ESP32 memory acceptance remains unavailable.')
    (out / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    expected = 1 if profile == 'simulator-X3' else 6
    assert len(list(out.glob('*.pgm'))) == expected
print(f'BookSync F35b {profile}: move/resize/focus/cancel/stale/restart PASS; {expected} captures')

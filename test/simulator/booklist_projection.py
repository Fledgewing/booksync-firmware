#!/usr/bin/env python3
"""F40w Book list provider policy in each native profile; no new UI is enabled."""
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
out = root / 'reports' / profile / 'booklist-projection'
out.mkdir(parents=True, exist_ok=True)
start = time.monotonic()
with tempfile.TemporaryDirectory(prefix='booksync-booklist-') as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_BOOKLIST_PROJECTION_SCENARIO='1', CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'booklist.log').write_text(log)
    if result.returncode or 'BookSync Book list projection PASS:' not in log:
        raise SystemExit(f'Book list projection {profile} failed; see {out}/booklist.log')
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
(out / 'metrics.json').write_text(json.dumps({
    'elapsed_seconds': time.monotonic() - start,
    'native_peak_rss_bytes': usage.ru_maxrss * (1 if sys.platform == 'darwin' else 1024),
    'internal_heap_free': None, 'largest_internal_block': None, 'task_stack_watermark': None,
    'hardware_memory_note': 'Native RSS does not qualify ESP32 heap or stack safety.'
}, indent=2) + '\n')
print(f'Book list projection {profile}: nine presets and independent states PASS')

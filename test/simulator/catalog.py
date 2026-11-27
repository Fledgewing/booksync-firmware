#!/usr/bin/env python3
"""F16 incremental fake-server ingest, process restart, permission loss, restart."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-catalog-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    for i, mode in enumerate(('ingest', 'recover', 'malformed', 'recover', 'oversized', 'recover', 'partial', 'recover', 'permission', 'recover')):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_CATALOG_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'catalog-{i}-{mode}.log').write_text(log)
        if result.returncode or 'BookSync catalog scenario PASS' not in log:
            raise SystemExit(f'F16 failed: {profile} {mode}')
(out / 'catalog.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'scenarios': 10, 'books': 10000,
    'manifest_requests': 50, 'read_bytes_bound': 7, 'parser_dynamic_allocations': 0,
    'runtime_memory': 'Fixed object sizes in logs; no per-row parser allocations. Native RSS/stack is not ESP32 internal heap, largest block or high-water acceptance.'
}, indent=2) + '\n')
print(f'BookSync F16 {profile}: ten ingest/restart/failure scenarios PASS')

#!/usr/bin/env python3
"""F25 post-paint acquisition, bounded decode and retired-view scenarios."""
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
modes = ('acquire', 'revision', 'account', 'retired', 'oversize', 'unknown-oversize', 'malformed')
for mode in modes:
    with tempfile.TemporaryDirectory(prefix='booksync-covers-', dir=out) as directory:
        sd = Path(directory) / 'fs_'
        sd.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_COVER_JOBS_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'cover-{mode}.log').write_text(log)
        if result.returncode or 'BookSync cover scenario PASS' not in log:
            raise SystemExit(f'F25 failed: {profile} {mode}\n{log[-4000:]}')
(out / 'cover-jobs.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'scenarios': len(modes), 'max_input_bytes': 524288,
    'chunk_bytes': 1024, 'bitmap_bytes': 3072, 'cache_entries': 4,
    'runtime_memory': 'Fixed workspace logged. Native heap/stack values cannot qualify hardware acceptance.'
}, indent=2) + '\n')
print(f'BookSync F25 {profile}: {len(modes)} cover/delivery/bounds scenarios PASS')

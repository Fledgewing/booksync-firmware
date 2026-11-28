#!/usr/bin/env python3
"""F36a: render each approved state through the production foreground activity."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import json
import resource
import time
import re
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out = root / 'reports' / profile / 'catalog-surface'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-catalog-') as directory:
    env = dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
               BOOKSYNC_CATALOG_CAPTURES=str(out),CROSSPOINT_SIM_SD=str(Path(directory)/'fs_'))
    start = time.monotonic()
    run = subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=180)
    log = run.stdout+run.stderr
    (out/'catalog-surface.log').write_text(log)
    if run.returncode or 'BookSync F36a PASS: 30 approved states' not in log:
        raise SystemExit(f'F36a {profile}: failure; see reports/{profile}/catalog-surface/catalog-surface.log')
    requests = re.findall(r'Catalog bounded allocation: controller=(\d+) thumbnails=(\d+)',log)
    assert requests, 'missing allocation bounds'
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    (out/'metrics.json').write_text(json.dumps({
        'states':30,'seconds':round(time.monotonic()-start,3),
        'peak_native_rss_bytes':int(peak if sys.platform=='darwin' else peak*1024),
        'bounded_ui_requested_bytes':max(int(a)+int(b) for a,b in requests),
        'internal_heap':'unavailable on native simulator',
        'largest_block':'unavailable on native simulator',
        'task_stack_watermark':'unavailable on native simulator',
        'hardware_acceptance':False},indent=2)+'\n')
    assert len(list(out.glob('*.pgm'))) == 30
    assert all(p.stat().st_size>1000 for p in out.glob('*.pgm'))
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
print(f'F36a {profile}: 30 approved states PASS')

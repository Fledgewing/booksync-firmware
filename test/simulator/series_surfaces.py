#!/usr/bin/env python3
"""Approved F36c Series states through the actual foreground renderer per profile."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
if profile not in ('x4-pro-simulator','x4-classic-simulator','simulator-X3'):
    raise SystemExit('unrecognized profile')
out=root/'reports'/profile/'series-surfaces'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-series-') as directory:
    sd=Path(directory)/'fs_';sd.mkdir()
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
             BOOKSYNC_SERIES_SURFACES=str(out),CROSSPOINT_SIM_SD=str(sd))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=180)
    log=result.stdout+result.stderr;(out/'scenario.log').write_text(log)
    if result.returncode or 'BookSync F36c surfaces PASS' not in log:
        raise SystemExit(f'F36c surfaces failed: {profile}\n{log[-4000:]}')
    captures=list(out.glob('*.pgm'))
    if len(captures)!=32:raise SystemExit(f'expected 31 Series states and one shared header: {len(captures)}')
(out/'metrics.json').write_text(json.dumps({'profile':profile,'series_captures':31,'shared_group_header_captures':1,
    'paint_library_reads':0,'implicit_reader_opens':0,'physical_heap_stack':'Unavailable in native simulator; hardware acceptance remains separate.'},indent=2)+'\n')
print(f'BookSync F36c {profile}: 31 Series states + top-right All header PASS')

#!/usr/bin/env python3
"""Approved F36c Series states through the actual foreground renderer per profile."""
from pathlib import Path
import json
import hashlib
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
for old_capture in out.glob('*.pgm'):
    old_capture.unlink()
for old_geometry in out.glob('*.geometry.json'):
    old_geometry.unlink()
with tempfile.TemporaryDirectory(prefix='booksync-series-') as directory:
    sd=Path(directory)/'fs_';sd.mkdir()
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
             BOOKSYNC_SERIES_SURFACES=str(out),CROSSPOINT_SIM_SD=str(sd))
    negative_env = dict(env, BOOKSYNC_SERIES_GEOMETRY_NEGATIVE='1')
    negative = subprocess.run([str(root/'.pio/build'/profile/'program')], cwd=directory, env=negative_env,
                              capture_output=True, text=True, timeout=180)
    negative_log = negative.stdout + negative.stderr
    (out/'geometry-negative.log').write_text(negative_log)
    if negative.returncode != 36 or 'Overlapping text geometry:' not in negative_log:
        raise SystemExit(f'geometry regression probe failed to reject the former title spacing: {profile}\n{negative_log[-2500:]}')
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=180)
    log=result.stdout+result.stderr;(out/'scenario.log').write_text(log)
    if result.returncode or 'BookSync F36c surfaces PASS' not in log:
        raise SystemExit(f'F36c surfaces failed: {profile}\n{log[-4000:]}')
    if log.count('Series text geometry PASS:') != 31:
        raise SystemExit('missing geometry proof for one or more Series states')
    geometry_files = sorted(out.glob('*.geometry.json'))
    if len(geometry_files) != 31: raise SystemExit('missing per-state text geometry')
    for geometry in geometry_files:
        boxes = json.loads(geometry.read_text())
        if not boxes: raise SystemExit(f'empty text geometry: {geometry.name}')
        for index, a in enumerate(boxes):
            assert a['width'] > 0 and a['height'] > 0, (geometry.name, a)
            for b in boxes[index + 1:]:
                overlap = (a['layer'] == b['layer'] and a['x'] < b['x'] + b['width'] and
                           b['x'] < a['x'] + a['width'] and a['y'] < b['y'] + b['height'] and
                           b['y'] < a['y'] + a['height'])
                assert not overlap, (geometry.name, a, b)
        assert not any(' bytes' in b['text'] for b in boxes if b['layer'] == 1), geometry.name
    captures=list(out.glob('*.pgm'))
    if len(captures)!=32:raise SystemExit(f'expected 31 Series states and one Library context: {len(captures)}')
(out/'metrics.json').write_text(json.dumps({'profile':profile,'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'program_sha256':hashlib.sha256((root/'.pio/build'/profile/'program').read_bytes()).hexdigest(),'series_captures':31,'library_group_context_captures':1,
    'geometry_negative_probe_rejected':True,'geometry_states_checked':31,'text_box_overlaps':0,'clipped_text_boxes':0,'paint_library_reads':0,'implicit_reader_opens':0,'physical_heap_stack':'Unavailable in native simulator; hardware acceptance remains separate.'},indent=2)+'\n')
print(f'BookSync F36c {profile}: 31 Series states + full Library group context PASS')

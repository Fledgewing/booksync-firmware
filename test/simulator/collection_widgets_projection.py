#!/usr/bin/env python3
"""F40w Library/Lists provider policies and both approved list sizes; no UI."""
from pathlib import Path
import os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'collection-widget-projections'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-collection-widgets-') as directory:
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',BOOKSYNC_COLLECTION_WIDGET_PROJECTION_SCENARIO='1',
             CROSSPOINT_SIM_SD=str(Path(directory)/'fs_'))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=120)
    log=result.stdout+result.stderr
    (out/'projections.log').write_text(log)
    assert not result.returncode and 'BookSync collection widgets projection PASS:' in log, f'Projection {profile} failed: {out}/projections.log'
print(f'BookSync Library/Lists projection {profile}: two-row accounting and both list sizes PASS')

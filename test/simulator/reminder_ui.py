#!/usr/bin/env python3
"""R1 native rendering and independent safe physical input routes per profile."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
if profile not in ('x4-pro-simulator','x4-classic-simulator','simulator-X3'):
    raise SystemExit('unknown reminder profile')
out=root/'reports'/profile/'reminder-ui'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-reminder-ui-') as directory:
    sd=Path(directory)/'fs_';sd.mkdir()
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),BOOKSYNC_REMINDER_UI=str(out))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=90)
    (out/'scenario.log').write_text(result.stdout+result.stderr)
    if result.returncode or 'BookSync R1 reminder UI PASS' not in result.stdout+result.stderr:
        raise SystemExit(f'reminder UI failed: {result.returncode}; see reports/{profile}/reminder-ui/scenario.log')
    if len(list(out.glob('*.pgm')))!=35:
        raise SystemExit('incomplete reminder captures')
print(f'BookSync R1 {profile}: 35 captures, memo-only paint, independent safe input routes PASS')

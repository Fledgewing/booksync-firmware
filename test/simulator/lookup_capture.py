#!/usr/bin/env python3
"""Bounded native word capture against synthetic SD, on every simulator profile."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'lookup-capture'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='source-',dir=out) as directory:
    sd=Path(directory)/'fs_'
    sd.mkdir()
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
             BOOKSYNC_LOOKUP_CAPTURE_SCENARIO='1',CROSSPOINT_SIM_SD=str(sd))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,
                          capture_output=True,text=True,timeout=60)
    log=result.stdout+result.stderr
    (out/'lookup-capture.log').write_text(log)
    if result.returncode or 'BookSync lookup capture PASS: cases=8' not in log:
        raise SystemExit(f'{profile} lookup capture failed; see {out}/lookup-capture.log')
print(f'BookSync lookup capture {profile}: eight cases PASS')

#!/usr/bin/env python3
"""F40w Book list: all nine presets/states and native All/Back; X3 retained text."""
from pathlib import Path
import os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'booklist-cancel'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-booklist-ui-') as directory:
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',BOOKSYNC_APPROVED_COVERS=str(root/'test/fixtures/f40w-covers'), SDL_AUDIODRIVER='dummy',BOOKSYNC_BOOKLIST_CAPTURES=str(out),BOOKSYNC_BOOKLIST_CANCEL_SCENE='1',
             CROSSPOINT_SIM_SD=str(Path(directory)/'fs_'))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=900)
    log=result.stdout+result.stderr
    (out/'booklist.log').write_text(log)
    assert not result.returncode and 'BookSync Book list PASS:' in log, f'Book list {profile} failed: {out}/booklist.log'
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    expected=1 if profile=='simulator-X3' else 3
    assert len(list(out.glob('*.pgm')))==expected
print(f'BookSync Book list {profile}: accepted book open retired by footer Back PASS')

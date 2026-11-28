#!/usr/bin/env python3
"""Approved Discover/Recommended states, native actions and X3 exclusion."""
from pathlib import Path
import os,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'personal-cards';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-personal-cards-') as directory:
 env=dict(os.environ,SDL_VIDEODRIVER='dummy',BOOKSYNC_APPROVED_COVERS=str(root/'test/fixtures/f40w-covers'), SDL_AUDIODRIVER='dummy',BOOKSYNC_PERSONAL_CARDS_CAPTURES=str(out),CROSSPOINT_SIM_SD=str(Path(directory)/'fs_'))
 result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=900)
 log=result.stdout+result.stderr;(out/'cards.log').write_text(log)
 assert result.returncode==0 and 'BookSync personal cards PASS:' in log,f'Personal cards {profile} failed: {out}/cards.log'
 assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
 assert len(list(out.glob('*.pgm')))==(1 if profile=='simulator-X3' else 25 if profile=='x4-classic-simulator' else 17)
print(f'BookSync personal cards {profile}: states/Another/All/Back/X3 PASS')

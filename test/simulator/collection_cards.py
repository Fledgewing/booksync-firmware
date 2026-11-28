#!/usr/bin/env python3
"""Approved Library/Lists main and optional sizes, all states, real destinations."""
from pathlib import Path
import os, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'collection-cards';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-collection-cards-') as directory:
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',BOOKSYNC_COLLECTION_CARDS_CAPTURES=str(out),CROSSPOINT_SIM_SD=str(Path(directory)/'fs_'))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=900)
    log=result.stdout+result.stderr;(out/'cards.log').write_text(log)
    assert not result.returncode and 'BookSync collection cards PASS:' in log, f'Collection cards {profile} failed: {out}/cards.log'
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    assert len(list(out.glob('*.pgm')))==(1 if profile=='simulator-X3' else 71)
print(f'BookSync collection cards {profile}: both sizes, independent states and destinations PASS')

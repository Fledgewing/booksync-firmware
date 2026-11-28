#!/usr/bin/env python3
"""F40w Book list: all nine presets/states and native All/Back; X3 retained text."""
from pathlib import Path
import os, subprocess, sys, tempfile
def check_badge_pixels(directory):
    # The approved Orchard Map artwork has an empty top-right corner. Only
    # independently published on-card evidence may add a circle/check there.
    def ink(name):
        magic, size, maximum, data = (directory / (name + '-known.pgm')).read_bytes().split(b'\n', 3)
        assert magic == b'P5' and size == b'480 800' and maximum == b'255'
        return sum(data[y * 480 + x] < 128 for y in range(126, 140) for x in range(106, 120))
    assert ink('recent') >= 30, 'published on-card evidence must draw its badge'
    for preset in ('all-finished', 'pick-back-up', 'finished-year', 'unopened'):
        assert ink(preset) == 0, preset + ': artwork must not invent an on-card badge'

root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'booklist-widget'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-booklist-ui-') as directory:
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',BOOKSYNC_APPROVED_COVERS=str(root/'test/fixtures/f40w-covers'), SDL_AUDIODRIVER='dummy',BOOKSYNC_BOOKLIST_CAPTURES=str(out),
             CROSSPOINT_SIM_SD=str(Path(directory)/'fs_'))
    result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=900)
    log=result.stdout+result.stderr
    (out/'booklist.log').write_text(log)
    assert not result.returncode and 'BookSync Book list PASS:' in log, f'Book list {profile} failed: {out}/booklist.log'
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines())
    expected=1 if profile=='simulator-X3' else 126
    assert len(list(out.glob('*.pgm')))==expected
if profile != 'simulator-X3': check_badge_pixels(out)
print(f'BookSync Book list {profile}: preset/state/All/Back scenes PASS')

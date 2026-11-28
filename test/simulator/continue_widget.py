#!/usr/bin/env python3
"""F40w Continue reading: main-size scenes and independent states."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
def check_known_pixels(path, classic):
    # Independent approved 456x224 hero coordinates in the X4 480x800 fixture.
    magic, size, maximum, pixels = path.read_bytes().split(b'\n', 3)
    assert magic == b'P5' and size == b'480 800' and maximum == b'255'
    def white(x, y): return pixels[y * 480 + x] > 128
    assert all(white(x, y) for y in (195, 201) for x in range(156, 404)), 'approved seven-pixel progress bar'
    ink = [x for y in range(227, 257) for x in range(170, 250) if not white(x, y)]
    assert ink and abs((min(ink) + max(ink)) / 2 - 210) <= 2, 'Resume label must be centered'
    percent = [x for y in range(186, 212) for x in range(404, 457) if white(x, y)]
    assert percent and max(percent) >= 453, 'percentage must align to the right'
    assert white(210, 213) == classic, 'Classic Resume action focus outline'

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
out = root / 'reports' / profile / 'continue-widget'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-widgets-') as directory:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', BOOKSYNC_APPROVED_COVERS=str(root/'test/fixtures/f40w-covers'), SDL_AUDIODRIVER='dummy',
               BOOKSYNC_CONTINUE_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'continue.log').write_text(log)
    if result.returncode or 'BookSync Continue PASS:' not in log:
        raise SystemExit(f'Continue {profile} failed; see {out}/continue.log')
    assert not any('Font ' in line and 'not found' in line for line in log.splitlines()), 'Home used an unprepared font'
    for scene in ('known', 'empty', 'not-saved', 'off', 'unsupported', 'error', 'oversize', 'stale', 'long-title'):
        assert (out / f'{scene}.pgm').stat().st_size > 1000
if profile != 'simulator-X3': check_known_pixels(out/'known.pgm', profile == 'x4-classic-simulator')
print(f'BookSync Continue {profile}: nine scenes PASS')

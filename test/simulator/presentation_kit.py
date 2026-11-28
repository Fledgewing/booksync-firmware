#!/usr/bin/env python3
"""Render every shared component on the real profile; check measured text ink."""
from pathlib import Path
import os
import hashlib
import json
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
PROFILES = ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')


def check_geometry(path):
    frames = texts = 0
    seen = []
    for line in path.read_text().splitlines():
        kind, *numbers = line.split()
        values = list(map(int, numbers))
        if kind == 'frame':
            area = values
            seen = []
            frames += 1
            continue
        bx, by, bw, bh, x, y, w, h = values
        assert w >= 0 and h >= 0
        assert bx <= x and by <= y and x + w <= bx + bw and y + h <= by + bh, (line, 'text leaves container')
        ax, ay, aw, ah = area
        assert ax <= bx and ay <= by and bx + bw <= ax + aw and by + bh <= ay + ah, (line, 'container leaves screen')
        if w and h:
            for ox, oy, ow, oh in seen:
                assert x + w <= ox or ox + ow <= x or y + h <= oy or oy + oh <= y, (line, (ox, oy, ow, oh), 'text overlap')
            seen.append((x, y, w, h))
        texts += 1
    assert frames and texts
    return frames, texts


def main(profile):
    assert profile in PROFILES
    out = ROOT / 'reports' / profile / 'presentation-kit'
    out.mkdir(parents=True, exist_ok=True)
    geometry = out / 'geometry.txt'
    geometry.write_text('')
    with tempfile.TemporaryDirectory(prefix='booksync-fk-') as tmp:
        sd = Path(tmp) / 'fs_'
        sd.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                   BOOKSYNC_KIT_CAPTURES=str(out), BOOKSYNC_KIT_GEOMETRY=str(geometry))
        run = subprocess.run([str(ROOT / '.pio/build' / profile / 'program')], cwd=tmp, env=env,
                             text=True, capture_output=True, timeout=90)
        (out / 'scenario.log').write_text(run.stdout + run.stderr)
        assert run.returncode == 0 and 'BookSync FK kit PASS' in run.stdout + run.stderr, out
    assert len(list(out.glob('*.pgm'))) == 6
    frames, texts = check_geometry(geometry)
    (out / 'metrics.json').write_text(json.dumps({
        'profile': profile, 'component_renders': 6, 'frames': frames, 'text_runs': texts,
        'overflow': 0, 'overlap': 0,
        'program_sha256': hashlib.sha256((ROOT / '.pio/build' / profile / 'program').read_bytes()).hexdigest(),
        'note': 'Measured pre-clip ink bounds. Native simulation is not ESP32 heap/stack evidence.'
    }, indent=2) + '\n')
    print(f'FK {profile}: 6 component renders; {frames} frames / {texts} measured text runs; no overflow or overlap')


if __name__ == '__main__':
    main(sys.argv[1])

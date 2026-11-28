#!/usr/bin/env python3
"""B2 approved notice, actual Home rendering and fake Library dispatch."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
for choice in ('now', 'later', 'back'):
    out = root / 'reports' / profile / 'upgrade-notice' / choice
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='booksync-notice-') as directory:
        sd = Path(directory) / 'fs_'
        sd.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   CROSSPOINT_SIM_SD=str(sd), BOOKSYNC_NOTICE_CAPTURES=str(out), BOOKSYNC_NOTICE_CHOICE=choice)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=90)
        (out / 'scenario.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0 and 'BookSync B2 PASS' in result.stdout + result.stderr, out
        assert (out / 'upgrade-notice.pgm').is_file()
print(f'BookSync B2 {profile}: 3 scenarios PASS (Now/Later/Back)')

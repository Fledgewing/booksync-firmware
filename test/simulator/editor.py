#!/usr/bin/env python3
"""F35a real HAL editor intents, restart and approved presentation captures."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile / 'editor'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-editor-') as temporary:
    sd = Path(temporary) / 'fs_'
    sd.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_EDITOR_CAPTURES=str(out), CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                            cwd=temporary, env=env, capture_output=True, text=True, timeout=90)
    (out / 'scenario.log').write_text(result.stdout + result.stderr)
    if result.returncode or 'BookSync F35a PASS' not in result.stdout + result.stderr:
        raise SystemExit(f'F35a failed; see reports/{profile}/editor/scenario.log')
    expected = 10 if profile == 'simulator-X3' else 6
    assert len([p for p in out.glob('*.pgm') if p.stem != 'x3-settings-entry']) == expected
print(f'BookSync F35a {profile}: add/remove/reset/restart/stale/keep PASS; {expected} captures')

if profile == 'simulator-X3':
    with tempfile.TemporaryDirectory(prefix='booksync-settings-editor-') as temporary:
        sd = Path(temporary) / 'fs_'
        sd.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_EDITOR_CAPTURES=str(out), BOOKSYNC_EDITOR_SETTINGS='1', CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=temporary, env=env, capture_output=True, text=True, timeout=90)
        (out / 'settings-entry.log').write_text(result.stdout + result.stderr + f'\nexit={result.returncode}\n')
        assert result.returncode == 0 and 'BookSync X3 Settings entry/back PASS' in result.stdout + result.stderr
    print('BookSync X3 Settings entry/back PASS')

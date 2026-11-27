#!/usr/bin/env python3
"""F33 shared schema/class override, bad-layout retention and restart on real HAL."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
raw = {'schemaVersion': 1, 'revision': 3,
       'grids': {'android-tablet': {'cols': 4, 'rows': 8},
                 'x4': {'cols': 2, 'rows': 7}, 'x3': {'cols': 2, 'rows': 7}},
       'pages': [{'id': 'home', 'tab': 'Home', 'placements': [
           {'instance': 'future', 'widget': 'future-widget', 'version': 987,
            'x': 0, 'y': 0, 'w': 4, 'h': 2, 'settings': {'keep': [None, {}, []]}}]}],
       'overrides': {'x4': {'future': {'w': 2}}, 'x3': {'future': {'hidden': True}}},
       'unknown': {'keep': True}}
with tempfile.TemporaryDirectory(prefix='booksync-layout-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    (sd / 'layout').write_bytes(b'broken layout: preserve these bytes')
    shared = json.dumps(raw).encode()
    (sd / 'shared').write_bytes(shared)
    (sd / 'shared.tmp').write_bytes(b'{"schemaVersion":')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_LAYOUT_SCENARIO='1', CROSSPOINT_SIM_SD=str(sd))
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                            cwd=directory, env=env, capture_output=True, text=True, timeout=60)
    out = root / 'reports' / profile
    out.mkdir(parents=True, exist_ok=True)
    (out / 'layout.log').write_text(result.stdout + result.stderr)
    if result.returncode or 'BookSync layout scenario PASS' not in result.stdout + result.stderr:
        raise SystemExit(f'F33 failed; see reports/{profile}/layout.log')
    assert (sd / 'layout').read_bytes() == b'broken layout: preserve these bytes'
    assert (sd / 'shared.bak').read_bytes() == shared
print(f'BookSync F33 {profile}: shared layout / override / retained fallback / restart PASS')

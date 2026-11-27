#!/usr/bin/env python3
"""F18a production handler, real HAL, F15 accounts, F17 owner and a durable fake server."""
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
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
logs = []
boots = 0
with tempfile.TemporaryDirectory(prefix='booksync-match-') as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'match').mkdir(parents=True)
    def boot(mode, cut=None):
        global boots
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_MATCH_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        if cut is not None:
            env['BOOKSYNC_MATCH_CUT'] = str(cut)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=30)
        boots += 1
        logs.append(f'{mode} cut={cut}\n{result.stdout}{result.stderr}')
        (out / 'lifecycle-match.log').write_text('\n'.join(logs))
        expected = 86 if cut is not None or mode in ('before', 'after') else 0
        if result.returncode != expected or (expected == 0 and 'BookSync match scenario PASS' not in result.stdout + result.stderr):
            raise SystemExit(f'match failed: {mode} cut={cut}; see reports/{profile}/lifecycle-match.log')
    boot('seed')
    seed = (sd / 'match/state').read_bytes()
    cases = [('run', None), ('before', None), ('after', None),
             ('timeout-before', None), ('timeout-after', None)] + [('run', cut) for cut in (1, 2, 3)]
    for mode, cut in cases:
        for path in (sd / 'match').iterdir():
            path.unlink()
        (sd / 'match/state').write_bytes(seed)
        boot(mode, cut)
        # Only unacknowledged attempts reconcile. Acknowledged phases stay saved.
        boot('run' if mode == 'run' and cut in (None, 3) else 'reconcile')
        boot('run')
        assert (sd / 'match/remote').read_bytes() == b'1'
(out / 'lifecycle-match.json').write_text(json.dumps({
    'profile': profile, 'scenarios': len(cases), 'power_cuts': 5, 'boots': boots,
    'result': 'PASS', 'match_allocations': 0, 'checkpoint_capacity': 1024,
    'runtime_memory': {'internal_heap': None, 'largest_block': None, 'stack_high_water': None,
                       'reason': 'native HAL cannot represent ESP32 heap/stack; maintainer hardware acceptance required'}
}, indent=2) + '\n')
print(f'BookSync F18a {profile}: {len(cases)} scenarios / 5 cuts / {boots} boots PASS')

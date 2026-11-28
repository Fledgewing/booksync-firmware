#!/usr/bin/env python3
"""F18d production handler, real HAL, F15 accounts, F17 owner and a durable fake server."""
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
with tempfile.TemporaryDirectory(prefix='booksync-state-') as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'state').mkdir(parents=True)
    def boot(mode, cut=None):
        global boots
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_STATE_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        if cut is not None:
            env['BOOKSYNC_STATE_CUT'] = str(cut)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=30)
        boots += 1
        logs.append(f'{mode} cut={cut}\n{result.stdout}{result.stderr}')
        (out / 'lifecycle-state.log').write_text('\n'.join(logs))
        expected = 86 if cut is not None or mode in ('before', 'after', 'apply-cut') else 0
        if result.returncode != expected or (expected == 0 and 'BookSync state scenario PASS' not in result.stdout + result.stderr):
            raise SystemExit(f'state failed: {mode} cut={cut}; see reports/{profile}/lifecycle-state.log')
    boot('seed')
    seed = (sd / 'state/state').read_bytes()
    cases = [('run', None), ('before', None), ('after', None),
             ('timeout-before', None), ('timeout-after', None), ('open', None), ('apply-cut', None)] + [('run', cut) for cut in (1, 2, 3, 5)]
    for mode, cut in cases:
        for path in (sd / 'state').iterdir():
            path.unlink()
        (sd / 'state/state').write_bytes(seed)
        boot(mode, cut)
        # Only unacknowledged attempts reconcile. Acknowledged phases stay saved.
        boot('run' if mode == 'run' and cut in (None, 3) else 'reconcile')
        boot('run')
        assert (sd / 'state/remote').read_bytes() == b'1'
(out / 'lifecycle-state.json').write_text(json.dumps({
    'profile': profile, 'scenarios': len(cases), 'power_cuts': 7, 'boots': boots,
    'result': 'PASS', 'state_allocations': 0, 'checkpoint_capacity': 1024,
    'runtime_memory': {'internal_heap': None, 'largest_block': None, 'stack_high_water': None,
                       'reason': 'native HAL cannot represent ESP32 heap/stack; maintainer hardware acceptance required'}
}, indent=2) + '\n')
print(f'BookSync F18d {profile}: {len(cases)} scenarios / 7 cuts / {boots} boots PASS')

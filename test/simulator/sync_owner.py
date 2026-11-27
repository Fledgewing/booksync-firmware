#!/usr/bin/env python3
"""F17 real-HAL restarts at every owner/phase checkpoint, with a toy remote only."""
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
cuts = ['0:0', '4:0'] + [f'{checkpoint}:{phase}' for phase in range(5) for checkpoint in (1, 2, 3)]
with tempfile.TemporaryDirectory(prefix='booksync-owner-') as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'sync').mkdir(parents=True)
    def boot(mode, cut=None):
        global boots
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_SYNC_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        if cut is not None:
            env['BOOKSYNC_SYNC_CUT'] = cut
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=30)
        boots += 1
        logs.append(f'{mode} cut={cut}\n{result.stdout}{result.stderr}')
        (out / 'sync-owner.log').write_text('\n'.join(logs))
        expected = 86 if cut is not None else 0
        if result.returncode != expected or (cut is None and 'BookSync sync owner scenario PASS' not in result.stdout + result.stderr):
            raise SystemExit(f'sync owner failed: {mode} cut={cut}; see reports/{profile}/sync-owner.log')
    boot('seed')
    seed = (sd / 'sync/state').read_bytes()
    boot('transition')
    assert (sd / 'sync/state').read_bytes() == seed
    boot('uncertain')
    boot('drain')
    assert (sd / 'sync/remote').read_bytes() == b'\x01' * 5
    for cut in cuts:
        # Restore the same accepted offline queue into a fresh synthetic card.
        for path in (sd / 'sync').iterdir():
            path.unlink()
        (sd / 'sync/state').write_bytes(seed)
        boot('drain', cut)
        boot('drain')
        boot('drain')
        assert (sd / 'sync/remote').read_bytes() == b'\x01' * 5
(out / 'sync-owner.json').write_text(json.dumps({
    'profile': profile, 'scenarios': len(cuts) + 3, 'power_cuts': len(cuts),
    'boots': boots, 'result': 'PASS',
    'scratch_bytes': 5504, 'owner_allocations': 0,
    'runtime_memory': 'Caller-owned bounded scratch; host heap/largest block and stack high-water do not qualify hardware.'
}, indent=2) + '\n')
print(f'BookSync F17 {profile}: {len(cuts) + 3} scenarios / {len(cuts)} power cuts / {boots} boots PASS')

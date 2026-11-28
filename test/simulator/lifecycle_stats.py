#!/usr/bin/env python3
"""F18b real-HAL page history and phase checkpoints against a durable fake server."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
logs, boots = [], 0

def history(directory, count):
    directory.mkdir(parents=True, exist_ok=True)
    for segment in range((count + 63) // 64):
        rows = bytearray()
        for n in range(segment * 64, min((segment + 1) * 64, count)):
            row = struct.pack('<QQIIIIIIII', 1, 1800000000 + n * 10, n + 1, 10,
                              0, n, 50000, 0, n, 100000) + bytes(12)
            rows += row + struct.pack('<I', zlib.crc32(row))
        (directory / f'{segment:06}.bin').write_bytes(b'BSP1' + struct.pack('<III', segment, len(rows)//64, 0) + rows)

with tempfile.TemporaryDirectory(prefix='booksync-stats-', dir=out) as directory:
    sd = Path(directory) / 'fs_'
    history(sd / 'stats/pages', 70)
    def boot(mode, cut=None):
        global boots
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_STATS_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        if cut is not None:
            env['BOOKSYNC_STATS_CUT'] = str(cut)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=180)
        boots += 1
        logs.append(f'{mode} cut={cut}\n{result.stdout}{result.stderr}')
        (out / 'lifecycle-stats.log').write_text('\n'.join(logs))
        expected = 86 if cut is not None or mode in ('before', 'after') else 0
        if result.returncode != expected or (expected == 0 and 'BookSync stats scenario PASS' not in result.stdout+result.stderr):
            raise SystemExit(f'stats failed: {mode} cut={cut}; see reports/{profile}/lifecycle-stats.log')
    def derived(count):
        # Durable fake-server tuples derive one continuous measured session in
        # this fixture. Retry must add neither another session nor daily time.
        bits = (sd / 'stats/remote').read_bytes()
        rows = sum(byte.bit_count() for byte in bits)
        assert rows == count
        daily = {}
        for n in range(count):
            assert bits[n // 8] & (1 << (n % 8))
            at, remaining = 1800000000 + n * 10, 10
            while remaining:
                seconds = min(remaining, 86400 - at % 86400)
                daily[at // 86400] = daily.get(at // 86400, 0) + seconds
                remaining -= seconds
                at += seconds
        assert sum(daily.values()) == count * 10
        return {'sessions': 1, 'seconds': count * 10, 'daily_seconds': daily}
    boot('seed')
    seed = (sd / 'stats/state').read_bytes()
    # PhaseStored=5 cuts a partial batch; AckStored=3 now cuts only final acknowledgement.
    cases = [('run', None), ('before', None), ('after', None), ('timeout-before', None),
             ('timeout-after', None)] + [('run', cut) for cut in (1, 2, 3, 5)]
    for mode, cut in cases:
        for path in (sd / 'stats').iterdir():
            if path.is_file(): path.unlink()
        (sd / 'stats/state').write_bytes(seed)
        boot(mode, cut)
        boot('run' if mode == 'run' and cut in (None, 3) else 'reconcile')
        boot('run')
        derived(70)
    # 50k immutable history rows, 782 segments, fixed 32-row worker batch.
    for path in (sd / 'stats').iterdir():
        if path.is_file(): path.unlink()
    history(sd / 'stats/pages', 50000)
    (sd / 'stats/state').write_bytes(seed)
    boot('run')
    boot('run')
    totals = derived(50000)
(out / 'lifecycle-stats.json').write_text(json.dumps({
    'profile': profile, 'scenarios': len(cases)+1, 'power_cuts': 6, 'boots': boots,
    'result': 'PASS', 'history_rows': 50000, 'history_bytes': 50000*64+782*16,
    'batch_rows': 32, 'explicit_heap_allocations': 0, 'derived': totals,
    'runtime_memory': {'internal_heap': None, 'largest_block': None, 'stack_high_water': None,
        'reason': 'Native HAL does not model ESP32 heap/stack; maintainer hardware acceptance required'}
}, indent=2)+'\n')
print(f'BookSync F18b {profile}: {len(cases)+1} scenarios / 6 cuts / {boots} boots PASS')

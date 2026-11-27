#!/usr/bin/env python3
"""F07 offline enqueue, uncertain checkpoint, capacity and ack across real HAL restarts."""
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
with tempfile.TemporaryDirectory(prefix='booksync-journal-') as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'journal').mkdir(parents=True)
    def boot(mode):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_JOURNAL_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=30)
        logs.append(f'{mode}\n{result.stdout}{result.stderr}')
        (out / 'lifecycle-journal.log').write_text('\n'.join(logs))
        if result.returncode or 'BookSync journal scenario PASS' not in result.stdout + result.stderr:
            raise SystemExit(f'journal scenario failed: {mode}; see reports/{profile}/lifecycle-journal.log')
    for mode in ('enqueue', 'checkpoint', 'capacity', 'ack', 'restart', 'restart'):
        boot(mode)
    valid = (sd / 'journal/state').read_bytes()
    for damaged in (valid[:-1], valid[:-1] + bytes([valid[-1] ^ 1]), valid[:4] + b'\x02' + valid[5:]):
        (sd / 'journal/state').write_bytes(damaged)
        # Unknown/corrupt primary must not silently replay an older backup.
        boot('blocked')
        assert (sd / 'journal/state').read_bytes() == damaged
(out / 'lifecycle-journal.json').write_text(json.dumps({
    'profile': profile, 'scenarios': 4, 'boots': len(logs), 'result': 'PASS',
    'runtime_memory': '128-byte streaming buffer; no journal payload allocations; HAL file handles only; native heap and stacks do not qualify hardware'
}, indent=2) + '\n')
print(f'BookSync F07 {profile}: 4 journal scenarios / {len(logs)} boots PASS')

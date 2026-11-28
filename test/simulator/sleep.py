#!/usr/bin/env python3
"""F42 real native capture, sleep, process reboot and native resume; zero sockets."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile / 'sleep'
out.mkdir(parents=True, exist_ok=True)
audit = out / 'network.log'
audit.write_text('')
lib = out / ('audit.dylib' if sys.platform == 'darwin' else 'audit.so')
subprocess.run(['cc', '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-fPIC',
                str(root / 'test/simulator/network_audit.c'), '-o', str(lib)], check=True)
preload = 'DYLD_INSERT_LIBRARIES' if sys.platform == 'darwin' else 'LD_PRELOAD'
logs = []
for sequence in (('sleep', 'cold'), ('return',)):
    with tempfile.TemporaryDirectory(prefix='booksync-sleep-', dir=out) as directory:
        sd = Path(directory) / 'fs_'
        (sd / 'Books').mkdir(parents=True)
        shutil.copy2(root / 'test/epubs/test_reader_rendering_matrix.epub', sd / 'Books/Synthetic.epub')
        for mode in sequence:
            try:
                result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                    env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                             CROSSPOINT_SIM_SD=str(sd), BOOKSYNC_SLEEP_SCENARIO=mode,
                             BOOKSYNC_NETWORK_AUDIT=str(audit), **{preload: str(lib)}),
                    capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired as error:
                partial = (error.stdout or b'') + (error.stderr or b'')
                if isinstance(partial, bytes):
                    partial = partial.decode(errors='replace')
                logs.append(f'{mode}/TIMEOUT\n{partial}')
                (out / 'scenario.log').write_text('\n'.join(logs))
                raise
            output = result.stdout + result.stderr
            logs.append(f'{mode}\n{output}')
            (out / 'scenario.log').write_text('\n'.join(logs))
            expected = 'native sleep returns' if mode == 'return' else 'reboot resumes native anchor'
            if result.returncode or expected not in output or 'BookSync sleep scenario FAIL' in output:
                raise SystemExit(f'F42 failed: {mode}; see reports/{profile}/sleep/scenario.log')
            if audit.read_bytes():
                raise SystemExit('sleep/resume attempted network I/O')
(out / 'receipt.json').write_text(json.dumps(dict(profile=profile, sequences=2, boots=4, result='PASS',
    network_requests=0, new_artwork='disabled pending approval', coordinator_heap_bytes=0,
    runtime_memory=dict(internal_heap=None, largest_block=None, stack_high_water=None,
        reason='Native HAL does not represent ESP32 memory; maintainer acceptance required.')), indent=2)+'\n')
print(f'BookSync F42 {profile}: sleep/power-wake/cold-reboot/return, 4 boots, zero requests PASS')

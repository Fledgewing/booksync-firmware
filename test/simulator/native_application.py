#!/usr/bin/env python3
"""F46b2a native SD application/restart, account isolation and deliberate local cuts."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('Unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('native_recovery', root / 'src/booksync/server/bookorbit/recovery.py')
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
processes = 0
records_checked = 0


def inspect(sd):
    global records_checked
    for name in ('native-app.progress-capture', 'native-app.state-capture', 'native-app.state', 'native-app.position'):
        path = sd / '.booksync' / name
        if path.exists():
            recovery.inspect_record(path.read_bytes())
            records_checked += 1


def native_bytes(sd):
    return {path.relative_to(sd).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (sd / '.crosspoint').rglob('*') if path.is_file()}


with tempfile.TemporaryDirectory(prefix='booksync-native-app-', dir=out) as temporary:
    directory = Path(temporary)
    captured = directory / 'captured'
    (captured / 'Books').mkdir(parents=True)
    (captured / '.crosspoint').mkdir()
    shutil.copyfile(root / 'test/epubs/test_br_section_break.epub', captured / 'Books/Synthetic.epub')
    prefix = struct.pack('<HHHIBBI', 0, 2, 10, 123, 0xc5, 1, 7)
    (captured / '.crosspoint/native-progress.bin').write_bytes(prefix + struct.pack('<I', zlib.crc32(prefix)))
    (captured / '.crosspoint/native-progress.bin.bak').write_bytes(b'retained native backup\0bytes')
    (captured / '.crosspoint/native-legacy.bin').write_bytes(b'retained legacy observations\0unknown fields')
    revision = hashlib.sha256((captured / 'Books/Synthetic.epub').read_bytes()).hexdigest()
    original_native = native_bytes(captured)

    def run(sd, mode, label, cut=None):
        global processes
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                   BOOKSYNC_NATIVE_APPLICATION_SCENARIO=mode, BOOKSYNC_NATIVE_APPLICATION_REVISION=revision)
        if cut is not None:
            env['BOOKSYNC_NATIVE_APPLICATION_CUT'] = str(cut)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=90)
        processes += 1
        log = result.stdout + result.stderr
        (out / f'native-application-{label}.log').write_text(log)
        expected = 86 if cut is not None else 0
        if result.returncode != expected or (cut is None and 'BookSync native application scenario PASS' not in log):
            raise SystemExit(f'F46b2a failed: {profile} {label}; see retained log')
        if native_bytes(sd) != original_native:
            raise SystemExit(f'F46b2a changed native reader bytes: {profile} {label}')
        return log

    run(captured, 'capture', 'capture')
    inspect(captured)
    baseline = directory / 'baseline'
    shutil.copytree(captured, baseline)
    log = run(baseline, 'apply', 'apply')
    checkpoints = int(re.search(r'checkpoints=(\d+)', log).group(1))
    if checkpoints < 12:
        raise SystemExit('Missing native application cut checkpoints')
    inspect(baseline)
    run(baseline, 'restart', 'restart')
    inspect(baseline)
    cuts = sorted({0, 1, checkpoints // 3, checkpoints // 2, checkpoints - 2, checkpoints - 1})
    for cut in cuts:
        sd = directory / f'cut-{cut}'
        shutil.copytree(captured, sd)
        run(sd, 'apply', f'cut-{cut}', cut)
        run(sd, 'apply', f'recover-{cut}')
        run(sd, 'restart', f'replay-{cut}')
        inspect(sd)
    for mode in ('wrong-scope', 'account-switch', 'changed-source'):
        sd = directory / mode
        shutil.copytree(captured, sd)
        if mode == 'changed-source':
            book = sd / 'Books/Synthetic.epub'
            changed = bytearray(book.read_bytes())
            changed[-1] ^= 1
            book.write_bytes(changed)
        before = {path.name: path.read_bytes() for path in (sd / '.booksync').iterdir() if path.is_file()}
        run(sd, mode, mode)
        after = {path.name: path.read_bytes() for path in (sd / '.booksync').iterdir() if path.is_file()}
        if before != after:
            raise SystemExit('Rejected native application mutated private authority')
        inspect(sd)
(out / 'native-application.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'processes': processes, 'deliberate_cuts': len(cuts),
    'recovery_records_checked': records_checked, 'native_reader_bytes': 'unchanged',
    'network': 'none; concrete SD providers receive normalized synthetic replies',
    'memory': 'Native fixtures do not measure ESP32 heap or task high-water'
}, indent=2) + '\n')
print(f'BookSync F46b2a {profile}: {processes} processes, {len(cuts)} deliberate cuts PASS')

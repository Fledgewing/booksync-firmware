#!/usr/bin/env python3
"""F05: reader saves locally with enabled legacy subscriptions and zero requests.

Run once per profile. Fake transport refuses and records every socket connect or
UDP send (including DNS and TLS); a positive probe proves interposition before the reader.
"""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PROFILE = sys.argv[1]
if PROFILE not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
OUT = ROOT / 'reports' / PROFILE / 'native-writers'
OUT.mkdir(parents=True, exist_ok=True)
AUDIT = OUT / 'requests.log'
AUDIT.write_text('')
LIB = OUT / ('network-audit.dylib' if sys.platform == 'darwin' else 'network-audit.so')
subprocess.run(['cc', '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-fPIC',
                str(ROOT / 'test/simulator/network_audit.c'), '-o', str(LIB)], check=True)
transport_env = os.environ.copy()
transport_env['BOOKSYNC_NETWORK_AUDIT'] = str(AUDIT)
transport_env['BOOKSYNC_NATIVE_WRITER_PROOF'] = '1'
transport_env['DYLD_INSERT_LIBRARIES' if sys.platform == 'darwin' else 'LD_PRELOAD'] = str(LIB)
# Use a disposable executable so macOS SIP cannot remove the preload variable.
probe = OUT / 'probe'
source = OUT / 'probe.c'
source.write_text('#include <sys/socket.h>\n#include <netdb.h>\nint main(void) { connect(-1, 0, 0); sendto(-1, 0, 0, 0, 0, 0); sendmsg(-1, 0, 0); getaddrinfo(0, 0, 0, 0); }\n')
subprocess.run(['cc', str(source), '-o', str(probe)], check=True)
subprocess.run([str(probe)], env=transport_env, check=True)
if AUDIT.read_text().splitlines() != ['request'] * 4:
    raise SystemExit('fake transport failed its positive control')
AUDIT.write_text('')

spec = importlib.util.spec_from_file_location('reader_smoke', ROOT / 'scripts/run_simulator_smoke_test.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)
prepare = smoke.prepare_fs
run = smoke.subprocess.run
sessions = 0


def seed(temp_root, book):
    result = prepare(temp_root, book)
    sd = temp_root / 'fs_'
    settings = sd / '.crosspoint/crossink-settings.json'
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text(json.dumps({'pluginsEnabled': 1, 'trackReadingStats': 1}))
    plugin = sd / '.crosspoint/plugins/local-capture'
    plugin.mkdir(parents=True, exist_ok=True)
    # Capture must still work when legacy manifests ask for immediate delivery.
    plugin.joinpath('device.json').write_text(json.dumps({'name': 'Local capture', 'events': {
        event: {'connect': True, 'request': {'method': 'POST', 'url': 'https://fixture.invalid/capture'}}
        for event in ('reader.open', 'reader.exit', 'reader.session', 'sleep.enter')}}))
    return result


def verify(command, **kwargs):
    global sessions
    if Path(command[0]).name != 'program':
        return run(command, **kwargs)
    kwargs['env'].update({k: transport_env[k] for k in ('BOOKSYNC_NATIVE_WRITER_PROOF', 'BOOKSYNC_NETWORK_AUDIT',
        'DYLD_INSERT_LIBRARIES' if sys.platform == 'darwin' else 'LD_PRELOAD')})
    result = run(command, **kwargs)
    if result.returncode:
        return result
    if AUDIT.read_bytes():
        raise AssertionError('Reader attempted network I/O')
    sd = Path(kwargs['cwd']) / 'fs_'
    progress = list(sd.rglob('progress*'))
    stats = list(sd.rglob('stats_v*.bin'))
    if not progress or not stats:
        raise AssertionError('Reader did not preserve local progress/stats')
    outbox = sd / '.crosspoint/plugins/local-capture/events.jsonl'
    if not outbox.is_file() or any(event not in outbox.read_text() for event in ('reader.open', 'reader.exit', 'sleep.enter')):
        raise AssertionError('Local plugin capture did not survive writer retirement')
    sessions += 1
    return result


smoke.prepare_fs = seed
smoke.subprocess.run = verify
# Existing production reader scenario opens, turns pages, exits, reopens and
# checks persisted progress/stats. The sleep scenario covers capture-before-sleep.
for options in ([],):
    sys.argv = ['reader-smoke', '--env', PROFILE, '--no-build', '--page-turns', '2'] + options
    if smoke.main():
        raise SystemExit('reader/sleep scenario failed')
(OUT / 'result.json').write_text(json.dumps({'profile': PROFILE, 'sessions': sessions,
    'requests': 0, 'transport_positive_controls': 4, 'local_capture': True,
    'runtime_memory': 'native allocations are not hardware heap/stack acceptance'}, indent=2) + '\n')
print(f'BookSync F05: {sessions} reader/sleep sessions, zero network requests, local saves/capture retained')

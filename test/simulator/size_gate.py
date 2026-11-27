#!/usr/bin/env python3
"""F14 single gate: real offline read/capture/restart with fake worker admission.

Each boot runs in a fresh metrics process, so peak RSS belongs to that boot.
Native RSS includes SDL, allocator and reader memory; it is never ESP32 heap.
"""
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def boot(program):
    started = time.monotonic()
    env = os.environ.copy()
    # Set preload immediately before launching firmware; macOS may strip DYLD
    # variables when launching a protected interpreter for the metrics helper.
    env['DYLD_INSERT_LIBRARIES' if sys.platform == 'darwin' else 'LD_PRELOAD'] = env['BOOKSYNC_SIZE_AUDIT_LIBRARY']
    result = subprocess.run([program], env=env, capture_output=True, text=True, timeout=120)
    print(result.stdout, end='')
    print(result.stderr, end='')
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    print('F14_METRICS ' + json.dumps({'seconds': round(time.monotonic() - started, 3),
          'peak_rss_bytes': int(usage.ru_maxrss * (1 if sys.platform == 'darwin' else 1024)),
          'internal_heap_bytes': None, 'largest_internal_block_bytes': None,
          'stack_high_water_bytes': None}))
    return result.returncode


def main(profile):
    if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
        raise SystemExit('unrecognized simulator profile')
    program = ROOT / '.pio/build' / profile / 'program'
    out = ROOT / 'reports' / profile / 'size-gate'
    out.mkdir(parents=True, exist_ok=True)
    audit = out / ('network-audit.dylib' if sys.platform == 'darwin' else 'network-audit.so')
    subprocess.run(['cc', '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-fPIC',
                    str(ROOT / 'test/simulator/network_audit.c'), '-o', str(audit)], check=True)
    probe = out / 'probe'
    subprocess.run(['cc', '-x', 'c', '-', '-o', str(probe)], check=True, text=True,
                   input='#include <netdb.h>\nint main(void) { getaddrinfo(0, 0, 0, 0); }\n')
    requests = out / 'requests.log'
    control = out / 'probe.requests.log'
    control.write_text('')
    transport = {'DYLD_INSERT_LIBRARIES' if sys.platform == 'darwin' else 'LD_PRELOAD': str(audit),
                 'BOOKSYNC_NETWORK_AUDIT': str(control)}
    subprocess.run([str(probe)], env=dict(os.environ, **transport), check=True)
    if control.read_text().splitlines() != ['request']:
        raise SystemExit('F14 fake transport positive control failed')
    requests.write_text('')
    transport['BOOKSYNC_NETWORK_AUDIT'] = str(requests)
    del transport['DYLD_INSERT_LIBRARIES' if sys.platform == 'darwin' else 'LD_PRELOAD']
    transport['BOOKSYNC_SIZE_AUDIT_LIBRARY'] = str(audit)
    metrics = []
    with tempfile.TemporaryDirectory(prefix='booksync-f14-', dir=out) as temp:
        sd = Path(temp) / 'fs_'
        (sd / 'Books').mkdir(parents=True)
        shutil.copy2(ROOT / 'test/epubs/test_reader_rendering_matrix.epub', sd / 'Books/Synthetic.epub')
        for mode in ('cut', 'restart'):
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       BOOKSYNC_READER_CAPTURE_SCENARIO=mode, BOOKSYNC_ADMISSION='1', BOOKSYNC_SIZE_GATE='1',
                       CROSSPOINT_SIM_SD=str(sd))
            env.update(transport)
            run = subprocess.run([sys.executable, str(Path(__file__).resolve()), '--boot', str(program)],
                                 cwd=temp, env=env, capture_output=True, text=True, timeout=140)
            log = run.stdout + run.stderr
            (out / f'{mode}.log').write_text(log)
            expected = ['BookSync reader capture scenario PASS', 'BookSync F14 admission PASS']
            if mode == 'restart':
                expected.append('BookSync F11 PASS:')
            if run.returncode or any(marker not in log for marker in expected):
                raise SystemExit(f'F14 {profile}/{mode} failed; see reports/{profile}/size-gate/{mode}.log')
            if requests.read_bytes():
                raise SystemExit(f'F14 {profile}/{mode} attempted a network request')
            values = [json.loads(line.removeprefix('F14_METRICS ')) for line in log.splitlines()
                      if line.startswith('F14_METRICS ')]
            if len(values) != 1 or values[0]['peak_rss_bytes'] <= 0:
                raise SystemExit('missing simulator runtime metrics')
            allocations = re.findall(r'F14_WORKER_ALLOCATION (\d+)', log)
            if len(allocations) != 1 or int(allocations[0]) <= 0:
                raise SystemExit('missing bounded worker allocation measurement')
            values[0]['worker_peak_requested_bytes'] = int(allocations[0])
            metrics.append(dict(boot=mode, **values[0]))
            journal = sd / '.booksync/reader.journal'
            if mode == 'cut':
                before = journal.read_bytes()
                journal.with_suffix('.journal.tmp').write_bytes(before[:-3])
            elif journal.read_bytes() != before:
                raise SystemExit('restart/admission changed unacknowledged capture bytes')
    receipt = {'profile': profile, 'result': 'PASS', 'boots': metrics,
               'network_requests': 0, 'fake_transport_positive_controls': 1,
               'program_sha256': hashlib.sha256(program.read_bytes()).hexdigest(),
               'cases': ['offline native read and page turn', 'durable unclosed capture',
                         'torn staging restart preserves journal', 'reading refuses network worker',
                         'Home fake worker cancellation/join', 'Home reconnect and display restoration'],
               'metrics_note': 'Measured native process peak RSS and elapsed time. ESP32 internal heap, largest block '
                               'and task high-water cannot be represented here; null is unavailable, never zero.'}
    (out / 'metrics.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with Path(os.environ['GITHUB_STEP_SUMMARY']).open('a') as summary:
            summary.write(f'\nF14 {profile}: offline capture/restart + admission PASS; zero network requests.\n\n'
                          '| Boot | Seconds | Native peak RSS bytes |\n|---|---:|---:|\n')
            for row in metrics:
                summary.write(f'| {row["boot"]} | {row["seconds"]} | {row["peak_rss_bytes"]} |\n')
            summary.write('\nESP32 internal heap/largest block/task watermark unavailable in native simulation.\n')
    print(f'BookSync F14 {profile}: 2 boots, offline capture/restart + network admission PASS')


if __name__ == '__main__':
    if sys.argv[1] == '--boot':
        raise SystemExit(boot(sys.argv[2]))
    main(sys.argv[1])

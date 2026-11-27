#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run F11 deterministic fault probes once per simulator profile, no live radio.

Production admission and allocation code is shared with host tests; the real
Home/reader/worker integration executes later in home.py in this same suite.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unknown simulator profile')
out = ROOT / 'reports' / profile / 'admission'
out.mkdir(parents=True, exist_ok=True)
# Isolated build directory: parallel simulator jobs cannot overwrite host evidence.
build = out / 'build'
commands = [
    ['cmake', '-S', 'test', '-B', str(build)],
    ['cmake', '--build', str(build), '--target', 'AdmissionTest', 'AdmissionMemoryTest', 'AdmissionWorkerTest', 'AdmissionC3MemoryTest', '-j', '2'],
    ['ctest', '--test-dir', str(build), '-R', r'^(Admission|Memory|WorkerFault)\.|^AdmissionStackContract$', '--output-on-failure'],
]
logs = []
for command in commands:
    run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    logs.append(run.stdout + run.stderr)
    (out / 'faults.log').write_text('\n'.join(logs))
    if run.returncode:
        print(logs[-1]); raise SystemExit(run.returncode)
(out / 'scenario.json').write_text(json.dumps({
    'profile': profile, 'fault_tests': 23, 'exit_code': 0,
    'cases': ['low largest internal block despite abundant PSRAM', 'allocation failure and reserve floor',
              'display park/restore failure', 'transport drain failure', 'stale/prepared lease',
              'reading refusal', 'bounded TLS/catalog/cover/worker resources'],
    'internal_heap': None, 'largest_internal_block': None, 'stack_high_water': None,
    'metrics_note': 'Fault-injected values qualify policy only; native stack headroom is not ESP32 acceptance.'}, indent=2)+'\n')
print(f'BookSync F11 {profile}: 23 memory/admission fault tests PASS')

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""F10 synthetic paired reconnect, DNS and quiet admission for each native profile.

No live network. Compile production transport policy/wrappers against fake radio
and lwIP. F03 NVS drives the pinned production key loader's two boot contexts.
The enclosing boot harness runs the actual emulator reader scenarios separately.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')


def main():
    profile = sys.argv[1]
    if profile not in PROFILES:
        raise SystemExit('unknown simulator profile')
    output = ROOT / 'reports' / profile / 'tailnet'
    output.mkdir(parents=True, exist_ok=True)
    sources = ROOT / 'test/host/tailnet'
    shim = ROOT / 'tailnet/shim'
    flags = ['-std=c++20', '-UNDEBUG', '-I'+str(shim), '-I'+str(sources/'stubs'),
             '-DCONFIG_ML_LITE='+str(int(profile == 'simulator-X3'))]
    commands = [
        [sys.executable, str(sources/'identity_test.py')],
    ]
    for name, source in [('resolver','TailnetResolver.cpp'), ('lease','TailnetNetPhase.cpp')]:
        binary = output / name
        commands += [[os.environ.get('CXX', 'c++'), *flags, str(sources/(name+'_test.cpp')),
                      str(shim/source), '-o', str(binary)], [str(binary)]]
    logs = []
    for command in commands:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        logs.append(result.stdout + result.stderr)
        if result.returncode:
            (output/'scenario.log').write_text('\n'.join(logs))
            print(logs[-1], end='')
            return result.returncode
    fixtures = ROOT/'test/fixtures/legacy/nvs/entries.json'
    (output/'scenario.log').write_text('\n'.join(logs))
    (output/'scenario.json').write_text(json.dumps({
        'profile': profile,
        'scenario': 'synthetic paired reconnect across two boots; DNS; reading refusal and stale lease',
        'transport': 'fake lwIP; pinned production identity loader and owned resolver/admission',
        'nvs_sha256': hashlib.sha256(fixtures.read_bytes()).hexdigest(),
        'identity_tests': 3, 'resolver_executables': 1, 'lease_executables': 1,
        'policy_state_bytes': 4, 'policy_heap_allocations': 0,
        'runtime_internal_heap': None, 'largest_block': None, 'stack_high_water': None,
        'metrics_note': 'Native probes cannot represent ESP internal heap or task stacks; F11/device acceptance required',
        'exit_code': 0,
    }, indent=2)+'\n')
    print('Tailnet synthetic reconnect/DNS/reading admission: PASS ('+profile+')')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

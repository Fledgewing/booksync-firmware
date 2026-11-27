#!/usr/bin/env python3
"""F01: neutral stub contract plus preserved offline reader boot on three profiles.

Each scenario gets disposable synthetic SD/NVS from the retained smoke harness.
No live transport, device, serial port or new product screen is used.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
subprocess.run([sys.executable, str(ROOT / 'tools/test/test-library-contract.py')], cwd=ROOT, check=True)
for profile in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    subprocess.run([sys.executable, str(ROOT / 'scripts/run_simulator_smoke_test.py'),
                    '--env', profile, '--page-turns', '2', *sys.argv[1:]], cwd=ROOT, check=True)
print('BookSync F01: three preserved offline boot/read/Back scenarios PASS')

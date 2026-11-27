#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""F26c: two synthetic NVS boots, read faults and retained legacy sources."""
import importlib.util
import json
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unknown profile')
spec = importlib.util.spec_from_file_location('identity_import', ROOT/'test/host/import_identity/run.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.run(profile)
out = ROOT/'reports'/profile/'identity-import'
out.mkdir(parents=True, exist_ok=True)
(out/'scenario.json').write_text(json.dumps({
    'profile': profile, 'scenario': 'two boots of production NVS audit with F03 synthetic storage',
    'source': 'read-only; all fixtures unchanged; no writes or generation APIs linked',
    'activation': False, 'independent_boot_processes': 2, 'application_heap_allocations': 0, 'secret_buffer_bytes': 32,
    'runtime_internal_heap': None, 'largest_block': None, 'stack_high_water': None,
    'metrics_note': 'Host NVS fault model cannot represent ESP heap or FreeRTOS stacks',
    'exit_code': 0}, indent=2)+'\n')

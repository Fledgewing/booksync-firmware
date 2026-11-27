#!/usr/bin/env python3
"""F26b: two fresh processes on synthetic SD, private F03 values only."""
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
fixtures = root / 'test/fixtures/legacy/sd/.crosspoint'
# Never read a device/card/NVS. Keep the F03 corpus itself byte-for-byte intact.
library = json.loads((fixtures / 'bookorbit.json').read_text())
library['serverUrl'] = 'synthetic-origin'
protected = dict(library)
library.pop('cfClientId')
library.pop('cfClientSecret_obf')
acquisition = json.loads((fixtures / 'opds.json').read_text())
acquisition['servers'][0]['url'] = 'synthetic-origin'
native = json.loads((fixtures / 'koreader.json').read_text())
native['serverUrl'] = 'synthetic-origin'
seeds = {'protected.json': json.dumps(protected).encode(),
         'native.json': json.dumps(native).encode(),
         'library.json': json.dumps(library).encode(),
         'acquisition.json': json.dumps(acquisition).encode(),
         'networks.json': (fixtures / 'wifi.json').read_bytes(),
         'corrupt.json': b'{"credentials":',
         'unknown.json': b'{"version":99}',
         'old.json': b'{"credentials":[{"ssid":"fake","password_obf":"b2xk"}]}'}
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-credentials-') as directory:
    sd = Path(directory) / 'fs_'
    (sd / 'legacy').mkdir(parents=True)
    (sd / '.booksync/import').mkdir(parents=True)
    for name, data in seeds.items():
        (sd / 'legacy' / name).write_bytes(data)
    snapshots = None
    for boot in range(2):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_CREDENTIALS_SCENARIO=str(boot), CROSSPOINT_SIM_SD=str(sd))
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        # Receipts contain counts only, never credential values or fingerprints.
        (out / f'credentials-boot-{boot}.log').write_text(log)
        if result.returncode or 'BookSync credentials scenario PASS: 18 checks; network=0' not in log:
            raise SystemExit(f'F26b failed; see reports/{profile}/credentials-boot-{boot}.log')
        for name, data in seeds.items():
            assert (sd / 'legacy' / name).read_bytes() == data, 'legacy source changed'
        current = {p.name: p.read_bytes() for p in (sd / '.booksync/import').iterdir()}
        if snapshots is not None:
            assert current == snapshots, 'second boot changed private proof/snapshot'
        snapshots = current
(out / 'credentials.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'boots': 2, 'policy_checks': 36,
    'network_requests': 0, 'legacy_sources_unchanged': True,
    'allocation_bound_bytes': 20480,
    'runtime_memory': 'One fallible reusable workspace below 20 KiB. Native RSS and stack do not qualify ESP32 internal heap, largest block or stack watermarks.'
}, indent=2) + '\n')
print(f'BookSync F26b {profile}: 36 checks across two boots PASS')

#!/usr/bin/env python3
"""F26a real-HAL read-only F03 import; two independent boots, every source retained."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
fixtures = root / 'test/fixtures/legacy'
entries = json.loads((fixtures / 'nvs/entries.json').read_bytes())['entries']
pin = bytes.fromhex(next(e['hex'] for e in entries if e['namespace'] == 'crossink_pin'))
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
logs = []
for fixture, status in [('sd/.crosspoint/crossink-settings.json', 1),
                        ('variants/settings/unknown.json', 2),
                        ('variants/settings/truncated.json', 3)]:
    with tempfile.TemporaryDirectory(prefix='booksync-settings-') as directory:
        sd = Path(directory) / 'fs_'
        # Seed the ENTIRE F03 SD/NVS inventory; no sign-in, identity, queue or path
        # can be changed as a side effect of settings import.
        for p in (fixtures / 'sd').rglob('*'):
            if p.is_file():
                target = sd / p.relative_to(fixtures / 'sd')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(p.read_bytes())
        source = sd / '.crosspoint/crossink-settings.json'
        source.write_bytes((fixtures / fixture).read_bytes())
        (sd / 'pin.fixture').write_bytes(pin)
        (sd / 'nvs.fixture').write_bytes((fixtures / 'nvs/entries.json').read_bytes())
        (sd / '.booksync/import/settings').mkdir(parents=True)
        before = {p.relative_to(sd): hashlib.sha256(p.read_bytes()).digest() for p in sd.rglob('*') if p.is_file()}
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   BOOKSYNC_SETTINGS_IMPORT_SCENARIO='1', BOOKSYNC_SETTINGS_EXPECT=str(status),
                   CROSSPOINT_SIM_SD=str(sd))
        first = None
        for boot in range(2):
            result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                    env=env, capture_output=True, text=True, timeout=60)
            logs.append(result.stdout + result.stderr)
            (out / 'settings-import.log').write_text('\n'.join(logs))
            if result.returncode or 'BookSync settings import scenario PASS' not in logs[-1]:
                raise SystemExit(f'F26a failed: {fixture} boot {boot}; see reports/{profile}/settings-import.log')
            after = {p.relative_to(sd): hashlib.sha256(p.read_bytes()).digest() for p in sd.rglob('*') if p.is_file()}
            assert all(after[p] == digest for p, digest in before.items()), 'legacy source changed'
            assert (sd / '.booksync/import/settings/native.raw').read_bytes() == source.read_bytes()
            assert (sd / '.booksync/import/settings/pin.raw').read_bytes() == pin
            if first is not None:
                assert after == first, 'second boot changed files'
            first = after
print(f'BookSync F26a {profile}: 3 scenarios / 6 boots PASS; full F03 inventory unchanged')

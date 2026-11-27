#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the production read-only NVS audit against F03 synthetic storage."""
from pathlib import Path
import json
import hashlib
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
def run(profile='host'):
    temporary = ROOT/'build/identity-tmp'
    temporary.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='booksync-identity-', dir=temporary) as tmp:
        tmp = Path(tmp)
        fixture = ROOT/'test/fixtures/legacy/nvs/entries.json'
        corpus = ROOT/'test/fixtures/legacy'
        def snapshot():
            return {str(p.relative_to(corpus)): hashlib.sha256(p.read_bytes()).digest()
                    for p in corpus.rglob('*') if p.is_file()}
        retained = snapshot()
        before = fixture.read_bytes()
        rows = []
        for e in json.loads(before)['entries']:
            if e['namespace'] == 'crossink_pin':
                continue
            if 'hex' in e:
                data = e['hex']
            elif e['type'] == 'str':
                data = (e['value']+'\0').encode().hex()
            else:
                data = e['value'].to_bytes({'u8':1,'u32':4,'u64':8}[e['type']], 'little').hex()
            rows.append(f"{e['namespace']} {e['key']} {e['type']} {data or '-'}")
        (tmp/'nvs.txt').write_text('\n'.join(rows)+'\n')
        flags = [] if os.uname().sysname == 'Darwin' else ['-lcrypto']
        subprocess.run([os.environ.get('CXX','c++'), '-std=c++20', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(ROOT/'src'), '-I'+str(Path(__file__).parent/'stubs'),
                        str(Path(__file__).parent/'IdentityTest.cpp'),
                        str(ROOT/'src/booksync/import/Identity.cpp'), '-o', str(tmp/'audit'), *flags], check=True)
        for boot in range(2):
            subprocess.run([str(tmp/'audit'), str(tmp/'nvs.txt')], check=True)
            assert snapshot() == retained
        print('Two independent process boots preserved the F03 corpus')
    print('Identity import scenario PASS ('+profile+')')
if __name__ == '__main__':
    run()

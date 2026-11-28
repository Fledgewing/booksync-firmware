#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""F24: real EPUB replacement, unchanged native sidecars, every publication cut."""
from pathlib import Path
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
old = (root / 'test/epubs/test_reader_rendering_matrix.epub').read_bytes()
# Same text/edition, changed metadata and ZIP offsets, as a library metadata edit.
buffer = io.BytesIO()
with zipfile.ZipFile(io.BytesIO(old)) as source, zipfile.ZipFile(buffer, 'w') as dest:
    for item in source.infolist():
        data = source.read(item.filename)
        if item.filename.endswith('.opf'):
            data = data.replace(b'</metadata>', b'<meta name="fixture" content="updated"/></metadata>')
        dest.writestr(item, data)
new = buffer.getvalue()
assert old != new
book = '/Books/Synthetic.epub'
hash_value = 14695981039346656037
for byte in book.encode():
    hash_value = ((hash_value ^ byte) * 1099511628211) & ((1 << 64) - 1)
cache = f'.crosspoint/epub_{hash_value}'
fixture = root / 'test/fixtures/legacy/sd/.crosspoint/epub_305419896'
boots = cuts = 0

def seed(sd):
    (sd / 'Books').mkdir(parents=True)
    (sd / book[1:]).write_bytes(old)
    (sd / 'candidate.epub').write_bytes(new)
    shutil.copytree(fixture, sd / cache)
    shutil.copy2(fixture / 'progress.bin', sd / 'f08-anchor.bin')
    for folder in ('bookmarks', 'clippings'):
        shutil.copytree(fixture.parent / folder, sd / '.crosspoint' / folder)
        original = next((fixture.parent / folder).glob('*.bin'))
        shutil.copy2(original, sd / '.crosspoint' / folder / f'epub_{zlib.crc32(book.encode())}.bin')
    for retained in fixture.parent.glob('book_*'):
        if retained.is_dir():
            shutil.copytree(retained, sd / '.crosspoint' / retained.name)
    # Preserve opaque/future sidecar fields as well as known native fixtures.
    (sd / cache / 'reader_settings.bin').write_bytes(b'future-reader-settings\0')
    (sd / '.crosspoint/highlights.json').write_bytes(b'{"highlights":[{"anchor":"/body/p[2]"}]}')
    return {p.relative_to(sd): p.read_bytes() for p in (sd / '.crosspoint').rglob('*') if p.is_file()}

def run(directory, mode, cut=None, native=False):
    global boots
    sd = directory / 'fs_'
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_COPY_JOBS_SCENARIO=mode, CROSSPOINT_SIM_SD=str(sd),
               BOOKSYNC_COPY_OLD=hashlib.sha256(old).hexdigest(),
               BOOKSYNC_COPY_NEW=hashlib.sha256(new).hexdigest())
    if cut is not None:
        env['BOOKSYNC_COPY_CUT'] = str(cut)
    if native:
        env['BOOKSYNC_LOCATOR_SCENARIO'] = '1'
    result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=120)
    log = result.stdout + result.stderr
    (out / f'copy-{mode}-{cut if cut is not None else "normal"}.log').write_text(log)
    expected = 86 if cut is not None else (73 if mode == 'link-cut' else 0)
    marker = 'BookSync locator scenario PASS' if native else 'BookSync copy scenario PASS'
    if result.returncode != expected or (expected == 0 and marker not in log):
        raise SystemExit(f'F24 {profile} {mode} cut={cut} failed ({result.returncode})\n{log[-4000:]}')
    boots += 1
    return log

for mode in ('update', 'current', 'ambiguous', 'reading', 'book-open', 'no-proof', 'digest', 'link-cut'):
    with tempfile.TemporaryDirectory(prefix='booksync-copy-', dir=out) as tmp:
        directory = Path(tmp)
        sd = directory / 'fs_'
        before = seed(sd)
        if mode == 'current':
            (sd / book[1:]).write_bytes(new)
        log = run(directory, mode)
        if mode == 'update':
            checkpoints = int(re.search(r'checkpoints=(\d+)', log)[1])
            workspace = int(re.search(r'workspace=(\d+)', log)[1])
            assert checkpoints > 50, 'publication cut hook did not exercise replacement'
            assert not (sd / (book[1:] + '.bak')).exists()
        if mode == 'link-cut':
            assert (sd / (book[1:] + '.bak')).read_bytes() == old
            run(directory, 'resume')
            assert not (sd / (book[1:] + '.bak')).exists()
        assert (sd / book[1:]).read_bytes() == (new if mode in ('update', 'link-cut', 'current') else old)
        for path, data in before.items():
            assert (sd / path).read_bytes() == data, path

for cut in range(checkpoints):
    with tempfile.TemporaryDirectory(prefix='booksync-copy-cut-', dir=out) as tmp:
        directory = Path(tmp)
        sd = directory / 'fs_'
        before = seed(sd)
        run(directory, 'update', cut)
        run(directory, 'resume')
        assert (sd / book[1:]).read_bytes() == new, cut
        for path, data in before.items():
            assert (sd / path).read_bytes() == data, (cut, path)
        cuts += 1

with tempfile.TemporaryDirectory(prefix='booksync-copy-native-', dir=out) as tmp:
    directory = Path(tmp)
    seed(directory / 'fs_')
    # Real reader opens updated ZIP, restores passage, reflows, closes and reopens.
    run(directory, 'native', native=True)

(out / 'copy-jobs.json').write_text(json.dumps({
    'profile': profile, 'result': 'PASS', 'boots': boots, 'power_cuts': cuts,
    'sidecars': 'Byte-identical progress, stats, settings, links and annotation state after replacement/cuts.',
    'native_reader': 'Updated EPUB restores native source anchor through reflow, close and reopen.',
    'payload_allocations': 0, 'fixed_workspace_bytes': workspace,
    'internal_heap': None, 'largest_internal_block': None, 'stack_high_water': None,
    'workspace': 'F23 workspace plus 32-byte old revision and one book-lifetime callback pointer.',
    'runtime_memory': 'Native host heap/stack values do not qualify device acceptance.'
}, indent=2) + '\n')
print(f'BookSync F24 {profile}: {boots} boots, {cuts} publication power cuts PASS')

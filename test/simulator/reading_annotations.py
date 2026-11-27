#!/usr/bin/env python3
"""F27b: two process boots preserve synthetic legacy sources and immutable archives."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile
import struct
import zlib
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unknown simulator profile')
fixtures = root / 'test/fixtures/legacy'
files = ['sd/.crosspoint/clippings/epub_0123456789abcdef0123456789abcdef.bin',
         'sd/.crosspoint/bookmarks/epub_0123456789abcdef0123456789abcdef.bin',
         'sd/.crosspoint/book_0123456789abcdef0123456789abcdef/bookorbit_annotations.bin',
         'sd/.crosspoint/book_0123456789abcdef0123456789abcdef/bookorbit_bookmarks.bin',
         'variants/bookmarks/bookorbit_bookmarks.bin',
         'variants/bod1/bookorbit_downloads.bin', 'sd/.crosspoint/bookorbit_downloads.bin']
def inventory(sd):
    return {p.relative_to(sd).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sd.rglob('*') if p.is_file()}
with tempfile.TemporaryDirectory(prefix='booksync-annotations-') as directory:
    sd = Path(directory) / 'fs_'
    sd.mkdir()
    for i, file in enumerate(files):
        (sd / f'source{i}').write_bytes((fixtures / file).read_bytes())
    # Full F03 inventory, including backups, corrupt and unknown formats, remains byte-identical.
    import shutil
    shutil.copytree(fixtures, sd / 'inventory')
    (sd / 'unknown').write_bytes(b'BOD9future bytes')
    before = inventory(sd)
    out = root / 'reports' / profile
    out.mkdir(parents=True, exist_ok=True)
    for boot in ('1', '2'):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   CROSSPOINT_SIM_SD=str(sd), BOOKSYNC_ANNOTATIONS_SCENARIO=boot)
        result = subprocess.run([str(root / '.pio/build' / profile / 'program')],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (out / f'annotations-{boot}.log').write_text(log)
        if result.returncode or 'BookSync annotation import PASS' not in log:
            raise SystemExit(f'F27b boot {boot} failed; see reports/{profile}/annotations-{boot}.log')
        after = inventory(sd)
        assert all(after.get(path) == digest for path, digest in before.items())
        for i, file in enumerate(files):
            archive = (sd / f'.booksync/import/annotations/archive{i}').read_bytes()
            magic, kind, size, path_size, provenance_size = struct.unpack('<4sIIII', archive[:20])
            assert magic == b'BSA1' and kind == (1, 2, 3, 4, 4, 5, 5)[i]
            assert archive[20:20+path_size] == f'/source{i}'.encode()
            assert archive[20+path_size:20+path_size+provenance_size] == b'synthetic;0.11;account-epoch:7;backup-preserved'
            raw = archive[20+path_size+provenance_size:-4]
            assert len(raw) == size and raw == (fixtures / file).read_bytes()
            assert struct.unpack('<I', archive[-4:])[0] == zlib.crc32(archive[:-4])
        if boot == '1':
            first = after
        else:
            assert after == first
print(f'BookSync F27b {profile}: 7 formats / 2 boots / full legacy inventory retained PASS')

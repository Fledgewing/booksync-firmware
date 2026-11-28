#!/usr/bin/env python3
"""Native source/application compiler frames; host evidence is not device watermarks."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
sources = [root / 'src/booksync/storage/SourceFile.cpp'] + [
    root / 'src/booksync/server/bookorbit' / (name + '.cpp') for name in
    ('ProgressCapture', 'NativeCaptureGate', 'NativeProgress', 'NativeStateStore', 'NativeState')]
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-native-frames-', dir=root / 'build') as temporary:
    directory = Path(temporary)
    for source in sources:
        subprocess.run([sys.argv[1], '-std=c++20', '-Os', '-fno-rtti', '-fstack-usage',
                        '-I' + str(root / 'lib/Memory'), '-I' + str(root / 'src'),
                        '-I' + str(root / 'test/host/file_publish/stubs'), '-I' + sys.argv[2],
                        '-c', str(source), '-o', str(directory / (source.stem + '.o'))],
                       check=True, capture_output=True, text=True)
    frames = []
    for report in directory.glob('*.su'):
        for line in report.read_text().splitlines():
            if not any(source.stem in line for source in sources):
                continue
            parts = line.split('\t')
            size = int(parts[-2])
            frames.append(size)
            if size > 512 or ('dynamic' in parts[-1] and 'bounded' not in parts[-1]):
                raise SystemExit('Native application stack regression: ' + line)
    if len(frames) < 20:
        raise SystemExit('Missing native application compiler frame evidence')
    print(f'Native application: {len(frames)} frames, maximum {max(frames)} bytes (host only)')

#!/usr/bin/env python3
"""Measure production policy frames; the host fault-card stub uses exceptions only for cuts."""
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='series-stack-') as directory:
    directory = Path(directory)
    for source,name in [('src/booksync/services/SeriesQueue.cpp','queue'),('src/booksync/storage/SeriesQueueStore.cpp','store')]:
        subprocess.run([sys.argv[1], '-std=c++20', '-Os', '-fno-rtti', '-fstack-usage',
                        '-I' + str(root / 'src'), '-I' + str(root / 'tailnet/shim'),
                        '-I' + str(root / 'test/host/file_publish/stubs'),
                        '-c', str(root / source), '-o', str(directory / (name+'.o'))], check=True)
    measured = []
    for evidence in directory.glob('*.su'):
        for line in evidence.read_text().splitlines():
            if 'booksync' not in line or not any(name in line for name in ('SeriesQueue', 'QueueCodec', 'queueConfirm')):
                continue
            parts = line.split('\t')
            size = int(parts[-2])
            measured.append(size)
            if size > 256 or 'dynamic' in parts[-1] and 'bounded' not in parts[-1]:
                raise SystemExit('Series queue stack regression: ' + line)
    if len(measured) < 6:
        raise SystemExit('missing Series compiler frame evidence')
    print(f'Series queue: {len(measured)} frames, max {max(measured)} bytes (host, not hardware watermark)')

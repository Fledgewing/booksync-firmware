#!/usr/bin/env python3
"""Prevent whole-page aggregate temporaries from returning to F22 worker/paint paths."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
compiler = sys.argv[1]
with tempfile.TemporaryDirectory(prefix='booksync-query-stack-') as directory:
    directory = Path(directory)
    for source in ('src/booksync/services/Queries.cpp', 'test/host/queries/StackProbe.cpp'):
        output = directory / (Path(source).stem + '.o')
        subprocess.run([compiler, '-std=c++20', '-Os', '-fstack-usage', '-fno-rtti',
                        '-I' + str(root / 'test/host/file_publish/stubs'), '-I' + str(root / 'src'),
                        '-c', str(root / source), '-o', str(output)], check=True)
    measured = []
    for evidence in directory.glob('*.su'):
        for line in evidence.read_text().splitlines():
            if not ('booksync' in line and any(name in line for name in ('Queries', 'QueryResult',
                                                 'QueryEntry', 'QueryRecord', 'StoredQuerySource'))) and 'copyQuerySnapshot' not in line:
                continue
            parts = line.split('\t')
            if len(parts) < 3:
                raise SystemExit('malformed stack evidence')
            size = int(parts[-2])
            measured.append(size)
            if size > 256 or 'dynamic' in parts[-1] and 'bounded' not in parts[-1]:
                raise SystemExit('query stack regression: ' + line)
    if len(measured) < 10:
        raise SystemExit('missing query stack evidence')
    print(f'BookSync query stack: {len(measured)} frames, max {max(measured)} bytes (host compiler, not hardware watermark)')

#!/usr/bin/env python3
"""Require byte-identical BookSync PR #200 size tooling and simulator pin."""
from pathlib import Path
import sys

FILES = ('budgets.json', 'size_report.py', 'test_size_report.py', 'simulator.json')
root = Path(__file__).resolve().parent
source = Path(sys.argv[1]) / 'tools/firmware'
for name in FILES:
    if (root / name).read_bytes() != (source / name).read_bytes():
        raise SystemExit(f'BookSync shared tooling differs: {name}')
print(f'BookSync shared tooling: {len(FILES)} identical files')

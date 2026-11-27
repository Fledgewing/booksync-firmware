#!/usr/bin/env python3
"""Restrict owned CI to reviewed triggers, read access and immutable Actions."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
workflows = list((root / '.github/workflows').iterdir())
if [p.name for p in workflows] != ['check.yml']:
    raise SystemExit('only BookSync check.yml may run')
workflow = workflows[0].read_text()
expected_events = 'on:\n  pull_request:\n    branches: [main]\n  push:\n    branches: [main]\n\n'
if workflow.split('\non:\n', 1)[-1].split('permissions:', 1)[0] != expected_events[4:]:
    raise SystemExit('only pull requests into main and pushes to main are allowed')
if workflow.count('branches: [main]') != 2 or 'contents: read' not in workflow:
    raise SystemExit('main-only read-only workflow required')
for path in [*workflows, root / '.github/actions/setup-platformio/action.yml']:
    text = path.read_text()
    if re.search(r'\bwrite\b|secrets\.|id-token:|(?m:^[ ]+permissions:)', text):
        raise SystemExit(f'publication credentials forbidden: {path.name}')
    for action in re.findall(r'uses:\s*(\S+)', text):
        if action.startswith('./') and action != './.github/actions/setup-platformio':
            raise SystemExit(f'unreviewed local Action: {action}')
        if not action.startswith('./') and not re.fullmatch(r'[^@]+@[0-9a-f]{40}', action):
            raise SystemExit(f'Action must be pinned: {action}')
    if text.count('actions/checkout@') != text.count('persist-credentials: false'):
        raise SystemExit('checkout must not persist credentials')
print('BookSync workflow supply-chain checks passed')

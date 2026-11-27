#!/usr/bin/env python3
"""F06: boot real simulator storage repeatedly with synthetic interrupted states."""
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
program = root / '.pio/build' / profile / 'program'
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
cases = (
    ('staging-only', {'state.tmp': 'complete-new'}, 'missing'),
    ('short-staging', {'state': 'complete-old', 'state.tmp': 'torn'}, 'complete-old'),
    ('between-renames', {'state.bak': 'complete-old', 'state.tmp': 'complete-new'}, 'complete-old'),
    ('invalid-primary', {'state': 'torn', 'state.bak': 'complete-old'}, 'complete-old'),
    ('after-rename', {'state': 'complete-new', 'state.bak': 'complete-old'}, 'complete-new'),
)
logs = []
for name, files, expected in cases:
    with tempfile.TemporaryDirectory(prefix='booksync-publication-') as directory:
        sd = Path(directory) / 'fs_/publication'
        sd.mkdir(parents=True)
        for path, value in files.items():
            (sd / path).write_text(value)
        for phase in ('read', 'publish', 'restart', 'restart-again'):
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       BOOKSYNC_PUBLICATION_EXPECT=expected if phase in ('read', 'publish') else 'complete-new',
                       CROSSPOINT_SIM_SD=str(Path(directory) / 'fs_'))
            env.pop('BOOKSYNC_PUBLICATION_WRITE', None)
            if phase == 'publish':
                env['BOOKSYNC_PUBLICATION_WRITE'] = '1'
            result = subprocess.run([str(program)], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            logs.append(f'{name}/{phase}\n{result.stdout}{result.stderr}')
            (out / 'file-publish.log').write_text('\n'.join(logs))
            if result.returncode or 'BookSync publication scenario PASS' not in result.stdout:
                raise SystemExit(f'publication scenario failed: {name}/{phase}')
(out / 'file-publish.json').write_text(json.dumps({'profile': profile, 'scenarios': len(cases),
    'boots': len(cases) * 4, 'result': 'PASS',
    'runtime_memory': '12-byte validation buffer; HAL handle allocation, no payload allocation; host metrics do not qualify hardware'}, indent=2) + '\n')
print(f'BookSync F06 {profile}: {len(cases)} publication scenarios / {len(cases) * 4} boots PASS')

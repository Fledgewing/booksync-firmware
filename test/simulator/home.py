#!/usr/bin/env python3
"""F04: real offline Home -> native EPUB -> mapped Back, once per profile."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROFILE = sys.argv[1]
assert PROFILE in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')
spec = importlib.util.spec_from_file_location('smoke', ROOT / 'scripts/run_simulator_smoke_test.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)
out = ROOT / 'reports' / PROFILE
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-home-') as temp:
    temp = Path(temp)
    book = smoke.prepare_fs(temp, smoke.DEFAULT_BOOK)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BOOKSYNC_ADMISSION='1', BOOKSYNC_HOME_BOOK=book, BOOKSYNC_HOME_CAPTURES=str(out / 'home-captures'))
    result = subprocess.run([str(smoke.program_path(PROFILE))], cwd=temp, env=env,
                            text=True, capture_output=True, timeout=120)
    log = result.stdout + result.stderr
    (out / 'home.log').write_text(log)
    print(log)
    if result.returncode or 'BookSync F04 PASS:' not in log or 'BookSync F11 PASS:' not in log:
        raise SystemExit(result.returncode or 1)
    assert len(list((out / 'home-captures').glob('*.pgm'))) == 4

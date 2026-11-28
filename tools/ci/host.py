#!/usr/bin/env python3
"""Run retained host tests and activate F01 guards atomically when available."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
def run(*args):
    subprocess.run(args, cwd=root, check=True)

run(sys.executable, 'test/host/test_presentation_geometry.py')
run(sys.executable, 'tools/ci/test_size_report.py')
run(sys.executable, 'tools/ci/test_harness.py')
run(sys.executable, 'tools/ci/test_size.py')
run(sys.executable, 'tools/ci/check_workflow.py')
guards = [root / 'tools/test' / name for name in
          ('test-ui-boundary.py', 'test-server-boundary.py')]
if (root / 'module-layers.json').exists() or any(p.exists() for p in guards):
    if not all(p.is_file() for p in guards):
        raise SystemExit('F01 is partial: both boundary guards are required')
    for guard in guards:
        run(sys.executable, str(guard))
else:
    print('F01 not landed: boundary guards pending', flush=True)
run('cmake', '-S', 'test', '-B', 'build/host')
run('cmake', '--build', 'build/host', '-j', '2')
run('ctest', '--test-dir', 'build/host', '--output-on-failure')

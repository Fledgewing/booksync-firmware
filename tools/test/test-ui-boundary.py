#!/usr/bin/env python3
"""Check role closure and exercise source/compile/link guard rejection cases."""
import subprocess
import sys
from boundary import ROOT, check_ui

errors = check_ui()
if errors:
    print('\n'.join(errors))
    raise SystemExit(1)
print('BookSync UI boundary: PASS', flush=True)
# F02 invokes this entry point. Its gate must prove rejection and link closure,
# not merely run a scanner against today's (currently empty) product UI tree.
subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'test/host',
                '-p', 'test_booksync_boundaries.py'], cwd=ROOT, check=True)
subprocess.run([sys.executable, str(ROOT / 'tools/test/test-library-contract.py')], cwd=ROOT, check=True)

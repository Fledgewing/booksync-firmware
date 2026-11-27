#!/usr/bin/env python3
"""F01 host gate: inventory, rejection tests, public link and server closure."""
import subprocess
import sys
from boundary import ROOT

for script in ('test-ui-boundary.py', 'test-server-boundary.py'):
    subprocess.run([sys.executable, str(ROOT / 'tools/test' / script)], cwd=ROOT, check=True)

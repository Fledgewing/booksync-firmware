#!/usr/bin/env python3
"""F19 durable policy and F12 one-draw exception on the real simulator HAL."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
if profile not in ('x4-pro-simulator','x4-classic-simulator','simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out=root/'reports'/profile
out.mkdir(parents=True,exist_ok=True)
logs=[]
sequences=[('seed','late','declined'),('seed','stale'),('seed','sleep','refresh-sleep','wake'),
           ('seed','override','override-restart'),('seed','conflict')]
boots=0
for sequence in sequences:
    with tempfile.TemporaryDirectory(prefix='booksync-position-',dir=out) as directory:
        sd=Path(directory)/'fs_'
        (sd/'position').mkdir(parents=True)
        for mode in sequence:
            result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,
                env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
                         CROSSPOINT_SIM_SD=str(sd),BOOKSYNC_POSITION_SCENARIO=mode),
                capture_output=True,text=True,timeout=30)
            boots+=1
            logs.append(f'{mode}\n{result.stdout}{result.stderr}')
            (out/'position-policy.log').write_text('\n'.join(logs))
            if result.returncode or 'BookSync position scenario PASS' not in result.stdout+result.stderr:
                raise SystemExit(f'position scenario failed: {mode}; see reports/{profile}/position-policy.log')
(out/'position-policy.json').write_text(json.dumps(dict(profile=profile,sequences=len(sequences),boots=boots,
    result='PASS',policy_allocations=0,read_workspace_bytes=16384,
    runtime_memory=dict(internal_heap=None,largest_block=None,stack_high_water=None,
        reason='Native HAL cannot represent ESP32 heap/stack; maintainer hardware acceptance required.')),indent=2)+'\n')
print(f'BookSync F19 {profile}: {len(sequences)} sequences / {boots} boots PASS')

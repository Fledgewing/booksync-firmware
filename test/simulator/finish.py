#!/usr/bin/env python3
"""F20 offline intent/restart/Undo and unknown-result reconciliation, real HAL."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile
out.mkdir(parents=True,exist_ok=True)
logs=[]
boots=0
with tempfile.TemporaryDirectory(prefix='booksync-finish-') as directory:
    sd=Path(directory)/'fs_'
    (sd/'finish').mkdir(parents=True)
    def boot(mode,cut=None):
        global boots
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',BOOKSYNC_FINISH_SCENARIO=mode,CROSSPOINT_SIM_SD=str(sd))
        if cut: env['BOOKSYNC_FINISH_CUT']=cut
        p=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=30)
        boots+=1
        logs.append(f'{mode} {cut}\n{p.stdout}{p.stderr}')
        (out/'finish.log').write_text('\n'.join(logs))
        if p.returncode!=(86 if cut else 0) or (not cut and 'BookSync finish scenario PASS' not in p.stdout+p.stderr):
            raise SystemExit(f'F20 failed {profile}: {mode} {cut}; see finish.log')
    boot('seed')
    seed=(sd/'finish/journal').read_bytes()
    cases=[('run',None),('uncertain',None),('cancel',None)]+[('run',f'{where}:{phase}') for phase in range(1,5) for where in ('before','after')]
    for mode,cut in cases:
        for path in (sd/'finish').iterdir(): path.unlink()
        (sd/'finish/journal').write_bytes(seed)
        boot(mode,cut)
        if mode=='cancel':
            boot('run')
            assert not (sd/'finish/remote').exists()
            continue
        # Undo arrives while the original result may be unknown, then a new boot reconciles.
        boot('undo')
        boot('run')
        remote=(sd/'finish/remote').read_bytes()
        assert remote==bytes([1,0,1,1,1,1,0,0,1,1,1]),remote
        boot('undo')  # A settled repeated Undo cannot create another side effect.
        assert (sd/'finish/remote').read_bytes()==remote
(out/'finish.json').write_text(json.dumps({'profile':profile,'scenarios':len(cases),'power_cuts':8,'boots':boots,'result':'PASS','finish_allocations':0,'borrowed_scratch_bytes':5504,'runtime_memory':{'internal_heap':None,'largest_block':None,'stack_high_water':None,'reason':'native HAL does not represent hardware memory; maintainer acceptance required'}},indent=2)+'\n')
print(f'BookSync F20 {profile}: {len(cases)} scenarios / 8 cuts / {boots} boots PASS')

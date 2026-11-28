#!/usr/bin/env python3
"""F29 real boot, Library retirement, exact SD retention, every publication cut."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
if profile not in ('x4-pro-simulator','x4-classic-simulator','simulator-X3'): raise SystemExit('unknown profile')
program=root/'.pio/build'/profile/'program'
corpus=root/'test/fixtures/legacy/sd'
out=root/'reports'/profile/'activation'
out.mkdir(parents=True,exist_ok=True)
boots=checks=cuts=0

def seed(sd):
    shutil.copytree(corpus,sd,dirs_exist_ok=True)
    cp=sd/'.crosspoint'
    for name in ('bookorbit.json','koreader.json','opds.json'):
        data=json.loads((cp/name).read_text())
        if name=='opds.json':
            original=data['servers'][0]
            data['servers']=[dict(original,url='synthetic-origin'),dict(original,url='synthetic-origin',username='synthetic-second')]
        else:
            data['serverUrl']='synthetic-origin'
            if name=='koreader.json': data['headers']=[]
        (cp/name).write_text(json.dumps(data))
    return {p.relative_to(sd):p.read_bytes() for p in sd.rglob('*') if p.is_file()}

def run(sd,mode='verify',cut=None,removed=False,dismissed=False,identity='retained'):
    global boots,checks,cuts
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
             CROSSPOINT_SIM_SD=str(sd),BOOKSYNC_ACTIVATION_SCENARIO=mode,
             BOOKSYNC_ACTIVATION_IDENTITY=identity)
    if identity is None: env.pop('BOOKSYNC_ACTIVATION_IDENTITY',None)
    for key in ('BOOKSYNC_ACTIVATION_CUT','BOOKSYNC_ACTIVATION_REMOVED','BOOKSYNC_ACTIVATION_DISMISSED'): env.pop(key,None)
    if cut is not None: env['BOOKSYNC_ACTIVATION_CUT']=str(cut)
    if removed: env['BOOKSYNC_ACTIVATION_REMOVED']='1'
    if dismissed: env['BOOKSYNC_ACTIVATION_DISMISSED']='1'
    result=subprocess.run([str(program)],cwd=sd.parent,env=env,capture_output=True,text=True,timeout=45)
    boots+=1
    log=result.stdout+result.stderr
    # Keep only scenario counts; no decoded values, NVS digests or host paths.
    match=re.search(r'BookSync activation scenario PASS: checks=(\d+) checkpoints=(\d+) network=0',log)
    if cut is not None:
        if result.returncode!=86: raise AssertionError(f'cut {cut} did not terminate at its boundary (exit {result.returncode})')
        cuts+=1; return 0
    if result.returncode or not match:
        # Full logs stay disposable and local; never add fixture/secret dumps to CI artifacts.
        (out/'failure.log').write_text(log)
        raise AssertionError(f'activation {mode} failed (exit {result.returncode}); see reports/{profile}/activation/failure.log')
    checks+=int(match[1]); return int(match[2])

def unchanged(sd,original):
    for path,data in original.items():
        if (sd/path).read_bytes()!=data: raise AssertionError(f'legacy SD bytes changed: {path}')

(root/'build/activation-tmp').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='booksync-activation-',dir=root/'build/activation-tmp') as directory:
    directory=Path(directory)
    fresh=directory/'fresh'
    fresh.mkdir()
    run(fresh,'fresh',identity=None); run(fresh,'fresh',identity=None)
    signed_out=directory/'signed-out'
    (signed_out/'.crosspoint').mkdir(parents=True)
    (signed_out/'.crosspoint/native-position').write_bytes(b'native-anchor')
    run(signed_out,'signed-out',identity=None); run(signed_out,'signed-out',identity=None)
    assert (signed_out/'.crosspoint/native-position').read_bytes()==b'native-anchor'
    base=directory/'baseline'
    original=seed(base)
    count=run(base)
    run(base); run(base)
    unchanged(base,original)
    for cut in range(count):
        sd=directory/f'cut-{cut}'
        seed(sd)
        run(sd,cut=cut)
        run(sd); run(sd)
        unchanged(sd,original)
        shutil.rmtree(sd)
    active=directory/'active'
    shutil.copytree(base,active)
    # Immutable activation/proof backups may be read, never repaired in place.
    # An old pending activation backup cannot revive an already dismissed notice.
    for name in ('activation','sync'):
        recovery=directory/f'recovery-{name}'
        shutil.copytree(active,recovery)
        current=recovery/'.booksync/import'/name
        shutil.copyfile(current,current.with_name(name+'.bak'))
        current.write_bytes(b'torn-owned-primary')
        run(recovery,'dismiss')
        count=run(recovery,dismissed=True)
        for cut in range(count):
            sd=directory/f'backup-{name}-{cut}'
            shutil.copytree(recovery,sd)
            run(sd,cut=cut,dismissed=True)
            run(sd,dismissed=True); run(sd,dismissed=True)
            unchanged(sd,original)
            assert (sd/'.booksync/import'/name).read_bytes()==b'torn-owned-primary'
            shutil.rmtree(sd)
    for mode in ('remove','dismiss'):
        probe=directory/f'{mode}-probe'
        shutil.copytree(active,probe)
        count=run(probe,mode)
        run(probe,removed=mode=='remove',dismissed=mode=='dismiss')
        for cut in range(count):
            sd=directory/f'{mode}-{cut}'
            shutil.copytree(active,sd)
            run(sd,mode,cut=cut)
            # A partial monotonic marker already denies restore/presentation.
            private=sd/'.booksync/import'
            removed=any(private.glob('removed-0*'))
            dismissed=any(private.glob('notice-done*'))
            run(sd,removed=removed,dismissed=dismissed)
            run(sd,removed=removed,dismissed=dismissed)
            unchanged(sd,original)
            shutil.rmtree(sd)
    retired=directory/'retired-without-markers'
    shutil.copytree(active,retired)
    run(retired,'remove'); run(retired,'dismiss',removed=True)
    private=retired/'.booksync/import'
    for pattern in ('removed-0*','notice-done*'):
        for path in private.glob(pattern): path.unlink()
    run(retired,removed=True,dismissed=True)
    (private/'state-0').write_bytes(b'torn-closed-state')
    (private/'state-10').unlink()
    run(retired,'state-unavailable',removed=True,dismissed=True)
    run(retired,'state-unavailable',removed=True,dismissed=True)
    unchanged(retired,original)
    # Missing/unknown activation state never falls back to legacy or new keys.
    damaged=directory/'damaged'
    shutil.copytree(active,damaged)
    (damaged/'.booksync/import/activation').write_bytes(b'future-schema')
    run(damaged,'unavailable'); unchanged(damaged,original)
    # F26c production audit's exhaustive NVS fault matrix runs beside this real
    # simulator boot matrix. Here, failed boot admission must remain offline.
    changed=directory/'changed'
    shutil.copytree(active,changed)
    run(changed,'identity-blocked',identity='changed')
    run(changed,'identity-blocked',identity='unreadable')
    unchanged(changed,original)

(out/'scenario.json').write_text(json.dumps({'profile':profile,'result':'PASS','boots':boots,
    'checks':checks,'hard_power_cuts':cuts,'legacy_sources_unchanged':True,'network_requests':0,
    'notice_ui_built':False,'application_workspace_bound_bytes':20480,
    'internal_heap':None,'largest_block':None,'stack_high_water':None,
    'metrics_note':'Native HAL and synthetic NVS are not ESP32 hardware-memory acceptance.'},indent=2)+'\n')
print(f'BookSync F29 {profile}: {checks} checks / {boots} boots / {cuts} hard power cuts PASS')

#!/usr/bin/env python3
"""Original native captures through the production personal source and Library."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out = root/'reports'/profile/'native-personal'
out.mkdir(parents=True,exist_ok=True)
def field(value):
    value=value.encode()
    return struct.pack('<I',len(value))+value
def fixture(mode):
    if mode=='bad-header': return b'torn original'
    count=0 if mode=='empty' else 1
    data=struct.pack('<BH',7,count)+field('Saved book')+field('Original author')+field('/books/one.epub')+struct.pack('<H',0)
    if not count: return data
    data+=struct.pack('<8HI',2,3,4,100,5,6,7,8,1700000000)+struct.pack('<I H II',0x1234,0xffff,100,200)
    key=('s'*16+'original-id') if mode=='scoped' else 'original-id'
    data+=field(key)+bytes([2 if mode=='scoped' else 0])+b'Saved chapter'.ljust(48,b'\0')
    text=b'x'*901 if mode=='oversize' else b'An original saved passage.'
    return data+struct.pack('<H',len(text))+text
modes=('ready','empty','missing','scoped','backup','bad-header','oversize','unknown-clock')
for mode in modes:
    with tempfile.TemporaryDirectory(prefix='booksync-native-personal-',dir=out) as directory:
        sd=Path(directory)/'fs_'
        (sd/'books').mkdir(parents=True)
        (sd/'books/one.epub').write_bytes(b'fixture; never opened by a reader')
        capture=sd/'.crosspoint/clippings'/f'epub_{zlib.crc32(b"/books/one.epub")}.bin'
        capture.parent.mkdir(parents=True)
        if mode!='missing':
            target=Path(str(capture)+'.bak') if mode=='backup' else capture
            target.write_bytes(fixture(mode))
            before=target.read_bytes()
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),BOOKSYNC_NATIVE_PERSONAL_SCENARIO=mode)
        result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=60)
        log=result.stdout+result.stderr
        (out/f'{mode}.log').write_text(log)
        if result.returncode or 'BookSync native personal PASS:' not in log:
            raise SystemExit(f'{profile} native personal failed: {mode}; see {out}/{mode}.log')
        if mode!='missing': assert target.read_bytes()==before,'original native capture changed'
(out/'metrics.json').write_text(json.dumps({'boots':len(modes),'index_allocation_cap_bytes':80000,'worker_object_cap_bytes':16384,'esp32_internal_free_bytes':None,'esp32_largest_block_bytes':None,'esp32_stack_high_water_bytes':None},indent=2))
print(f'BookSync native personal {profile}: {len(modes)} cases PASS')

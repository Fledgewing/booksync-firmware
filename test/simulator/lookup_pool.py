#!/usr/bin/env python3
"""Native saved-lookup pool: exact words, directory resume, no rewritten history."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
profile=sys.argv[1]
assert profile in ('x4-pro-simulator','x4-classic-simulator','simulator-X3')
out=root/'reports'/profile/'lookup-pool'
out.mkdir(parents=True,exist_ok=True)
for mode in ('known','reverse','empty','missing','backup','torn','malformed','unicode','retired'):
    with tempfile.TemporaryDirectory(prefix=mode+'-',dir=out) as directory:
        sd=Path(directory)/'fs_';sd.mkdir()
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',BOOKSYNC_LOOKUP_POOL_SCENARIO=mode,CROSSPOINT_SIM_SD=str(sd))
        result=subprocess.run([str(root/'.pio/build'/profile/'program')],cwd=directory,env=env,capture_output=True,text=True,timeout=60)
        log=result.stdout+result.stderr;(out/(mode+'.log')).write_text(log)
        assert result.returncode==0 and 'BookSync lookup pool PASS:' in log, mode
        for path in sd.rglob('dictionary_history.txt*'):
            assert path.suffix not in ('.tmp',) or path.read_bytes()==b'staging|D\n'
        # Native history is never replaced by a widget-owned cache or store.
        assert not list(sd.rglob('*vocabulary*')) and not list(sd.rglob('*lookup-pool*'))
print(f'BookSync lookup pool {profile}: nine cases PASS')

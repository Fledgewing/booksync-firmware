#!/usr/bin/env python3
"""F46a real HTTP adapter/worker checks, optionally against the existing E2E kit.

Set BOOKSYNC_LIVE_ORIGIN/USER/KEY to the isolated tools/e2e fault proxy and its
synthetic sync login to run against the seeded server. Without those variables,
CI uses a bounded socket fixture; it still exercises the production HTTP client.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading

root = Path(__file__).resolve().parents[2]
profile = sys.argv[1]
if profile not in ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3'):
    raise SystemExit('unrecognized simulator profile')
out = root / 'reports' / profile
out.mkdir(parents=True, exist_ok=True)
external = bool(os.environ.get('BOOKSYNC_LIVE_ORIGIN'))
state = {'mode': 'success', 'requests': 0}
user = os.environ.get('BOOKSYNC_LIVE_USER', 'synthetic-user')
key = os.environ.get('BOOKSYNC_LIVE_KEY', '0123456789abcdef0123456789abcdef')

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        state['requests'] += 1
        if state['mode'] == 'offline':
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        status = 200 if self.headers.get('x-auth-user') == user and self.headers.get('x-auth-key') == key else 401
        body = json.dumps({'items': [{'id': 1, 'title': 'Synthetic book', 'authors': [], 'files': []}],
                           'hasNext': False, 'nextCursor': None}).encode() if status == 200 else b'{}'
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

server = None
if external:
    origin = os.environ['BOOKSYNC_LIVE_ORIGIN']
else:
    server = ThreadingHTTPServer(('', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = 'http://' + socket.gethostbyname('localhost') + ':' + str(server.server_port)
try:
    with tempfile.TemporaryDirectory(prefix='booksync-live-adapter-', dir=out) as directory:
        sd = Path(directory) / 'fs_'
        sd.mkdir()
        modes = ('success', 'auth') if external else ('success', 'auth', 'offline')
        for mode in modes:
            state['mode'] = mode
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       BOOKSYNC_LIVE_ADAPTER_SCENARIO=mode, BOOKSYNC_LIVE_ORIGIN=origin,
                       BOOKSYNC_LIVE_USER=user, BOOKSYNC_LIVE_KEY=('0' * 32 if mode == 'auth' else key),
                       CROSSPOINT_SIM_SD=str(sd))
            result = subprocess.run([str(root / '.pio/build' / profile / 'program')], cwd=directory,
                                    env=env, capture_output=True, text=True, timeout=90)
            log = result.stdout + result.stderr
            (out / f'live-adapter-{mode}.log').write_text(log)
            if result.returncode or 'BookSync live adapter scenario PASS' not in log:
                raise SystemExit(f'F46a failed: {profile} {mode}\n{log[-4000:]}')
    (out / 'live-adapter.json').write_text(json.dumps({
        'profile': profile, 'result': 'PASS', 'scenarios': len(modes),
        'transport': 'existing tools/e2e server' if external else 'socket fixture',
        'network': 'admitted worker only', 'response_ram': 'SDK fixed streaming buffer',
        'hardware_memory': 'not measurable by host simulator'
    }, indent=2) + '\n')
    print(f'BookSync F46a {profile}: {len(modes)} real HTTP worker scenarios PASS')
finally:
    if server:
        server.shutdown()
        server.server_close()

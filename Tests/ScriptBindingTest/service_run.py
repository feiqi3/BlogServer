import http.server
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

seen = []
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_GET(self):
        seen.append(self.path)
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'ok')

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.daemon_threads = True
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'check.js'
        url = f'http://127.0.0.1:{server.server_port}'
        path.write_text('''
let started=false, busy=false, count=0;
async function onStart(){await ctx.http.get('URL/start'); started=true;}
async function onTick(){
 if(!started || busy) throw Error('overlap or startup order');
 busy=true;
 await new Promise(resolve=>ctx.after(80,resolve));
 const r=await ctx.http.get('URL/tick');
 if(r.status!==200) throw Error('request failed');
 count++; busy=false;
}
async function onStop(){
 if(busy || count<3) throw Error('stop order');
 await ctx.http.get('URL/stop');
}
'''.replace('URL', url), encoding='utf-8')
        for _ in range(2):
            result = subprocess.run([sys.argv[1], str(path)], timeout=20)
            if result.returncode: sys.exit(result.returncode)
        module = Path(directory) / 'check.mjs'
        (Path(directory) / 'dependency.js').write_text('export const version=1;', encoding='utf-8')
        module.write_text("import {version} from './dependency.js'; if(version!==1) throw Error('dependency failed');\n" + path.read_text().replace('async function', 'export async function'), encoding='utf-8')
        for _ in range(2):
            subprocess.run([sys.argv[1], str(module)], timeout=20, check=True)
        if seen[0] != '/start' or seen[-1] != '/stop' or seen.count('/tick') < 12 or seen.count('/start') != 4 or seen.count('/stop') != 4:
            raise RuntimeError(f'Lifecycle order failed: {seen}')
        path.write_text("async function onStart(){throw Error('startup failure')}\n", encoding='utf-8')
        subprocess.run([sys.argv[1], str(path), 'start-failure'], timeout=10, check=True)
        path.write_text("async function onTick(){throw Error('tick failure')}\n", encoding='utf-8')
        subprocess.run([sys.argv[1], str(path), 'tick-failure'], timeout=10, check=True)
        path.write_text("function onTick(){Promise.reject(Error('unhandled failure'))}\n", encoding='utf-8')
        subprocess.run([sys.argv[1], str(path), 'tick-failure'], timeout=10, check=True)
finally:
    server.shutdown()
    server.server_close()

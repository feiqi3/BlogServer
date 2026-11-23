import http.server
import subprocess
import sys
import tempfile
import threading
seen=[]
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args): pass
    def do_GET(self):
        seen.append(self.path)
        self.send_response(200)
        self.send_header('Content-Length','2')
        self.end_headers()
        self.wfile.write(b'ok')
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
server.daemon_threads=True
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    with tempfile.TemporaryDirectory() as root:
        subprocess.run([sys.argv[1],root,f'http://127.0.0.1:{server.server_port}'],check=True,timeout=35)
        for path in ['/v1/start','/v1/tick','/v1/stop','/v2/start','/v2/tick','/v2/stop']:
            if path not in seen: raise RuntimeError(f'Missing lifecycle request: {path}')
        if '/side-effect' in seen: raise RuntimeError('Candidate validation performed network I/O')
        if seen.count('/v1/start') != 3 or seen.count('/v2/start') != 1:
            raise RuntimeError('Duplicate activation or restart used an unexpected version')
finally:
    server.shutdown(); server.server_close()

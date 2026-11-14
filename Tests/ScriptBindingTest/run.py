import http.server
import subprocess
import sys
import threading
import time
seen = []

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_GET(self):
        seen.append(self.path)
        if self.path == '/slow': time.sleep(2)
        try:
            self.send_response(200)
            self.send_header('Content-Length', '5')
            self.end_headers()
            self.wfile.write(b'hello')
        except (BrokenPipeError, ConnectionResetError): pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.daemon_threads = True
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    result = subprocess.run([sys.argv[1], str(server.server_port)], timeout=15)
    if '/denied' in seen: raise RuntimeError('A forbidden destination received a request')
    sys.exit(result.returncode)
finally:
    server.shutdown()
    server.server_close()

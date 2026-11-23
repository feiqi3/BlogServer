"""Binary fixtures use temporary TLS material and loopback-only test policies."""
import http.server
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlsplit,parse_qs
seen=[]
class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    def handle_error(self,*args):pass
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def do_GET(self):
        route=urlsplit(self.path);seen.append(route.path)
        index=int(parse_qs(route.query).get('id',['0'])[0]);data=bytearray(range(256))*2;data[:4]=index.to_bytes(4,'little')
        status=200;headers=[('Content-Type','application/octet-stream')]
        if route.path=='/slow':time.sleep(.5)
        if route.path=='/hold':time.sleep(.15)
        if route.path=='/text':data=b'hello'
        if route.path=='/empty':status=204;data=b''
        if route.path=='/missing':status=404;data=b'\xff\x00'
        if route.path=='/redirect':status=302;data=b'';headers.append(('Location','/bytes'))
        if route.path=='/headers':headers.extend([('X-Repeat','one'),('X-Repeat','two')])
        if route.path=='/range':
            assert self.headers.get('Range')=='bytes=0-31'
            status=206;data=data[:32];headers.extend([('Content-Range','bytes 0-31/512'),('X-Probe',self.headers.get('X-Probe',''))])
        if route.path=='/large':data=b'x'*int(parse_qs(route.query)['size'][0])
        if route.path in ('/oversize','/oversize-chunked'):data=b'x'*1025
        chunked=route.path in ('/chunked','/oversize-chunked')
        try:
            self.send_response(status)
            for key,value in headers:self.send_header(key,value)
            if status!=204:
                self.send_header('Transfer-Encoding','chunked') if chunked else self.send_header('Content-Length',str(len(data)))
            self.end_headers()
            if route.path=='/truncated':
                self.wfile.write(data[:4]);self.wfile.flush();self.close_connection=True;return
            if chunked:
                for pos in range(0,len(data),31):
                    part=data[pos:pos+31];self.wfile.write(f'{len(part):x}\r\n'.encode()+part+b'\r\n')
                self.wfile.write(b'0\r\n\r\n');return
            if route.path=='/split':
                for pos in range(0,len(data),17):self.wfile.write(data[pos:pos+17]);self.wfile.flush()
            else:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
with tempfile.TemporaryDirectory(prefix='binary-http-') as directory:
    tmp=Path(directory);ca=tmp/'cert.pem';key=tmp/'key.pem'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1,DNS:localhost','-keyout',str(key),'-out',str(ca)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    plain=Server(('127.0.0.1',0),Handler);secure=Server(('127.0.0.1',0),Handler)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(ca,key);secure.socket=context.wrap_socket(secure.socket,server_side=True)
    for server in (plain,secure):threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        binary=Path(sys.argv[1]).resolve();source=Path(__file__).with_name('binary.mjs').resolve()
        subprocess.run([str(binary),f'http://127.0.0.1:{plain.server_port}',f'https://127.0.0.1:{secure.server_port}',str(ca),str(source)],check=True,cwd=tmp,timeout=25)
        assert not any(route.endswith('-never') for route in seen),seen
    finally:
        for server in (plain,secure):server.shutdown();server.server_close()

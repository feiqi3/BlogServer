"""Temporary certs/config; actual shared FeiLib HTTP server; no blog database."""
import concurrent.futures
import http.client
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time

root=Path(sys.argv[1]).resolve();build=Path(sys.argv[2]).resolve()
binary=build/'deferred-http-test'
includes=[root/'FeiLib/include',root/'FeiLib/3rd/json/include',root/'FeiLib/3rd/spdlog/include']
subprocess.run(['c++','-std=c++20','-pthread',*[f'-I{x}' for x in includes],str(root/'Tests/DeferredHttpTest/server.cpp'),f'-L{build}/Bin/Debug',f'-Wl,-rpath,{build}/Bin/Debug',f'-Wl,-rpath,{build}/debug','-lFeiLibD','-o',str(binary)],check=True)
def free_port():
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));return sock.getsockname()[1]
plain,tls=free_port(),free_port()
def get(path):
    connection=http.client.HTTPConnection('127.0.0.1',plain,timeout=8)
    try:
        connection.request('GET',path);response=connection.getresponse();body=response.read().decode()
        assert response.getheaders().count(('X-Finalized','once'))==1,response.getheaders()
        return response.status,body
    finally:connection.close()
def response(file):
    line=file.readline();assert line.startswith(b'HTTP/'),repr(line);status=int(line.split()[1]);headers={}
    while (line:=file.readline())!=b'\r\n':
        if not line:raise RuntimeError('Connection ended in headers')
        key,value=line.decode().split(':',1);headers[key.lower()]=value.strip()
    body=file.read(int(headers['content-length'])).decode()
    assert headers['x-finalized']=='once'
    return status,body
with tempfile.TemporaryDirectory(prefix='deferred-http-') as directory:
    tmp=Path(directory)
    (tmp/'server.cfg').write_text('env:test\n[[test]]\nLogShowLevel: trace\nLogSaveLevel: off\nALPNPreference: http2\nH2MaxStreamNum: 100\nH2EnablePush: 0\nHttpMaxDeferredRequests: 24\n')
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-keyout',str(tmp/'private.pem'),'-out',str(tmp/'cert.pem')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    with (tmp/'server.log').open('w+') as log:
        process=subprocess.Popen([str(binary),str(tmp),str(plain),str(tls)],cwd=tmp,stdout=log,stderr=subprocess.STDOUT)
        try:
            for _ in range(100):
                try:
                    if get('/fast/ready')==(200,'ready'):break
                except OSError:time.sleep(.1)
            else:raise RuntimeError('Server failed to listen')
            assert get('/unavailable/test')==(503,'unavailable')
            with socket.create_connection(('127.0.0.1',plain),timeout=5) as sock:
                sock.sendall(b'GET /later/first HTTP/1.1\r\nHost: test\r\n\r\nGET /fast/second HTTP/1.1\r\nHost:')
                time.sleep(.02);sock.sendall(b' test\r\n\r\n')
                file=sock.makefile('rb');assert response(file)==(200,'first');assert response(file)==(200,'second');file.close()
            with concurrent.futures.ThreadPoolExecutor(max_workers=20) as workers:
                pending=[workers.submit(get,f'/gate/{i}') for i in range(20)]
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if int(get('/stats/now')[1].split(',')[0])>=21:break
                    time.sleep(.02)
                assert get('/fast/available')==(200,'available')
                get('/release/all')
                assert [future.result() for future in pending]==[(200,str(i)) for i in range(20)]
            with concurrent.futures.ThreadPoolExecutor(max_workers=24) as workers:
                before=int(get('/stats/now')[1].split(',')[0])
                pending=[workers.submit(get,f'/gate/cap-{i}') for i in range(24)]
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if int(get('/stats/now')[1].split(',')[0])==before+24:break
                    time.sleep(.02)
                assert get('/later/overflow')[0]==429
                get('/release/all')
                assert [future.result() for future in pending]==[(200,f'cap-{i}') for i in range(24)]
            assert get('/timeout/late')[0]==408
            assert get('/throw/fail')[0]==500
            sock=socket.create_connection(('127.0.0.1',plain));sock.sendall(b'GET /later/closed HTTP/1.1\r\nHost: test\r\n\r\n');time.sleep(.03);sock.close();time.sleep(.25)
            assert int(get('/stats/now')[1].split(',')[1])>=2
            # Same pipeline over TLS HTTP/1.1, with certificate verification.
            context=ssl.create_default_context(cafile=str(tmp/'cert.pem'));context.set_alpn_protocols(['http/1.1'])
            with context.wrap_socket(socket.create_connection(('127.0.0.1',tls)),server_hostname='localhost') as sock:
                sock.sendall(b'GET /later/tls-first HTTP/1.1\r\nHost: test\r\n\r\nGET /fast/tls-second HTTP/1.1\r\nHost: test\r\n\r\n')
                file=sock.makefile('rb');assert response(file)==(200,'tls-first');assert response(file)==(200,'tls-second');file.close()
            print('PASS HTTP/1.1: plain/TLS pipeline ordering, 20 pending requests without blocking, bounded admission, timeout, exception, disconnect and late/double completion',flush=True)
            subprocess.run(['node',str(root/'Tests/DeferredHttpTest/http2.mjs'),str(tls),str(tmp/'cert.pem')],check=True,timeout=20)
            sock=socket.create_connection(('127.0.0.1',plain));sock.sendall(b'GET /gate/shutdown HTTP/1.1\r\nHost: test\r\n\r\n');time.sleep(.05)
            get('/shutdown/now');process.wait(timeout=15);sock.close();assert process.returncode==0
            print('PASS shutdown: pending responder cancellation before loop destruction',flush=True)
        except BaseException:
            print((tmp/'server.log').read_text(errors='replace')[-5000:]);raise
        finally:
            if process.poll() is None:process.kill();process.wait()

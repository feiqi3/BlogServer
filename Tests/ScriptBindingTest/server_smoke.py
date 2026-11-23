"""Run under `unshare -m -n --propagation private` as root. Uses schema-only fixtures."""
import http.server
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import urllib.error
import urllib.parse
import base64

binary = Path(sys.argv[1]).resolve()
resources = Path(sys.argv[2]).resolve()
admin = '--admin' in sys.argv
articles = '--articles' in sys.argv
profiling = '--profiling' in sys.argv
native_only = '--native-only' in sys.argv
memory_check = '--memory-limit-check' in sys.argv
friend_avatars = '--friend-avatars' in sys.argv
frontend = Path(sys.argv[sys.argv.index('--admin')+1]).resolve() if admin else None
subprocess.run(['ip', 'link', 'set', 'lo', 'up'], check=True)
subprocess.run(['ip', 'addr', 'add', '93.184.216.34/32', 'dev', 'lo'], check=True)
seen = []
avatar_state={'fail':False}
avatar_png=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/a9sAAAAASUVORK5CYII=')
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        seen.append(self.path)
        body=b'\x00\x80\xffA' if self.path=='/binary' else b'ok'
        if self.path.startswith('/friend/'):
            body=avatar_png
        self.send_response(503 if avatar_state['fail'] and self.path.startswith('/friend/') else 200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass

fixture = http.server.ThreadingHTTPServer(('0.0.0.0', 0), Handler)
fixture.daemon_threads = True
threading.Thread(target=fixture.serve_forever, daemon=True).start()
with tempfile.TemporaryDirectory(prefix='script-server-') as directory:
    tmp = Path(directory)
    isolated = tmp / 'resources'
    for name in ['config', 'database', 'SSL', 'web']:
        (isolated / name).mkdir(parents=True)
    shutil.copytree(resources / 'web/page', isolated / 'web/page')
    if frontend:
        shutil.copyfile(frontend / 'backyard-scripts.html', isolated / 'web/page/backyard-scripts.html')
    (isolated / 'web/assets').mkdir()
    shutil.copyfile(resources / 'config/h2PushPromise.cfg', isolated / 'config/h2PushPromise.cfg')
    with sqlite3.connect(f'file:{resources}/database/blog.db?mode=ro', uri=True) as original:
        schema = original.execute("select sql from sqlite_master where type='table' and sql is not null and name not like 'sqlite_%'").fetchall()
    with sqlite3.connect(isolated / 'database/blog.db') as database:
        for (statement,) in schema: database.execute(statement)
        if articles or friend_avatars: database.execute("INSERT INTO Categories(id,name,categorypic) VALUES(1,'Fixture','fixture.png')")
    (tmp / 'scripts').mkdir()
    source = tmp / 'scripts/job.mjs'
    url = f'http://93.184.216.34:{fixture.server_port}'
    (source.parent / 'request.js').write_text(f"export async function request(path){{return await ctx.http.get('{url}/'+path)}}")
    source.write_text("import {request} from './request.js';\n" + ''.join(f"export async function on{name}(){{await request('{route}')}}\n" for name, route in [('Start', 'start'), ('Tick', 'tick'), ('Stop', 'stop')]))
    if memory_check:
        # A retained 10 MiB buffer cannot load under the default 8 MiB cap.
        with source.open('a') as script:
            script.write("\nconst retained=new Uint8Array(10*1024*1024);retained[retained.length-1]=42;\n")
    (isolated / 'config/server.cfg').write_text(f'env:test\n[[test]]\nAdminUser: smoke\nAdminPassword: smoke-only\nLogShowLevel: info\nLogSaveLevel: off\nScriptFile: {source}\nScriptIntervalMs: 50\n'+('ScriptAdminEnabled: true\n' if admin else ''))
    if memory_check:
        with (isolated/'config/server.cfg').open('a') as configuration:
            configuration.write('ScriptMemoryLimitMiB: 16\n')
    if articles or friend_avatars:
        with (isolated/'config/server.cfg').open('a') as configuration:
            configuration.write(f'ALPNPreference: http2\nH2MaxStreamNum: 100\nH2EnablePush: 0\nProfilingEnabled: {str(profiling).lower()}\nProfilingDirectory: {tmp}/custom-profiles\n')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=localhost', '-addext','subjectAltName=DNS:localhost','-keyout', str(isolated / 'SSL/private.pem'), '-out', str(isolated / 'SSL/cert.pem')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(['mount', '--bind', str(isolated), str(resources)], check=True)
    process = None
    try:
        if memory_check:
            configuration=isolated/'config/server.cfg'
            valid_configuration=configuration.read_text()
            try:
                for invalid in ('0','-1','1.5','16MiB','1025','18446744073709551616',''):
                    probe_configuration=valid_configuration.replace('ScriptMemoryLimitMiB: 16',f'ScriptMemoryLimitMiB: {invalid}')
                    configuration.write_text(probe_configuration.replace(f'{tmp}/custom-profiles',f'{tmp}/invalid-memory-profiles'))
                    with (tmp/'invalid-memory.log').open('w') as invalid_log:
                        probe=subprocess.Popen([str(binary)],cwd=tmp,stdout=invalid_log,stderr=subprocess.STDOUT)
                        try:
                            for attempt in range(100):
                                if probe.poll() is not None:raise RuntimeError('Invalid memory setting crashed native Server')
                                try:
                                    with urllib.request.urlopen('http://127.0.0.1/api/categories',timeout=1) as response:
                                        if response.status==200:break
                                except OSError:pass
                                time.sleep(.05)
                            else:raise RuntimeError('Invalid memory setting blocked native routes')
                            if seen:raise RuntimeError('Invalid memory setting silently enabled scripts')
                        finally:
                            probe.terminate();probe.wait(timeout=10)
                    if 'ScriptMemoryLimitMiB must be an integer in 1..1024' not in (tmp/'invalid-memory.log').read_text():
                        raise RuntimeError('Invalid memory configuration was not reported')
            finally:
                configuration.write_text(valid_configuration)
            print('PASS Server memory validation: invalid, fractional, empty and overflowing values disable scripts and preserve native routes')
        if '--startup-stress' in sys.argv:
            # Temporary fixtures only. Test process startup repeatedly; graceful
            # shutdown and final persistence are covered by the normal path.
            with (tmp/'startup.log').open('w') as startup_log:
                environment=os.environ.copy()
                if '--trace-preload' in sys.argv:environment['LD_PRELOAD']=str(Path(sys.argv[sys.argv.index('--trace-preload')+1]).resolve())
                for index in range(100):
                    seen.clear()
                    process=subprocess.Popen([str(binary)],cwd=tmp,stdout=startup_log,stderr=subprocess.STDOUT,env=environment)
                    for attempt in range(100):
                        if process.poll() is not None:
                            raise RuntimeError(f'Startup iteration {index} exited ({process.returncode}): '+(tmp/'startup.log').read_text(errors='replace')[-4000:])
                        if seen.count('/tick')>=2:break
                        time.sleep(.05)
                    else:raise RuntimeError(f'Startup iteration {index} did not execute ticks')
                    process.terminate();process.wait(timeout=10);process=None
            seen.append('/stop')
            print('PASS 100 consecutive real Server startups: profiling, script restore, recurring HTTP and native loops')
            sys.exit(0)
        with (tmp / 'server.log').open('w+') as log:
            command=[str(binary)]
            if '--gdb' in sys.argv:command=['gdb','-q','-batch','-ex','run','-ex','thread apply all bt','--args',str(binary)]
            process = subprocess.Popen(command, cwd=tmp, stdout=log, stderr=subprocess.STDOUT)
            for attempt in range(100):
                if process.poll() is not None: raise RuntimeError(f'Server exited during startup ({process.returncode})')
                if native_only:
                    try:
                        urllib.request.urlopen('http://127.0.0.1/api/categories',timeout=1).close();break
                    except OSError:pass
                elif seen.count('/tick') >= 2: break
                time.sleep(.1)
            else: raise RuntimeError('Scheduled requests did not arrive')
            with urllib.request.urlopen('http://127.0.0.1/api/categories', timeout=5) as response:
                if response.status != 200: raise RuntimeError('Blog API failed')
                json.loads(response.read())
            if native_only:
                sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'ArticleHookTest'))
                from article_checks import native_exercise
                native_exercise(isolated)
            elif admin:
                if friend_avatars:
                    from avatar_cache_checks import exercise
                    exercise(url,seen,isolated,avatar_state,avatar_png)
                elif articles:
                    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'ArticleHookTest'))
                    from article_checks import exercise
                    exercise(url,seen,isolated,tmp,profiling)
                else:
                    from admin_smoke_checks import exercise
                    exercise(url, seen)
            stop_entries=seen.count('/stop-entered')
            try: urllib.request.urlopen('http://127.0.0.1/shutdown', timeout=5).close()
            except urllib.error.HTTPError as error:
                if error.code not in (302, 404): raise
            if articles and not native_only:
                from article_checks import shutdown_probe
                shutdown_probe(seen,stop_entries)
            process.wait(timeout=60)
            if process.returncode or (not native_only and (seen[0] != '/start' or seen[-1] != '/stop')):
                raise RuntimeError('Server lifecycle failed')
            print('PASS isolated Server: startup, blog API, graceful exit'+('' if native_only else ', recurring HTTP and stop hook'))
            if memory_check:
                print('PASS Server configuration: ScriptMemoryLimitMiB=16 permits a retained 10 MiB script buffer')
            if articles:
                from article_checks import persisted
                persisted(tmp,profiling,not native_only)
            if friend_avatars:
                # Restore the active package with the remote source unavailable.
                avatar_state['fail']=True;source.unlink()
                process=subprocess.Popen([str(binary)],cwd=tmp,stdout=log,stderr=subprocess.STDOUT)
                cache_url='http://127.0.0.1/avatar-cache?source='+urllib.parse.quote(url+'/friend/avatar.png?a=1&b=two%20words',safe='')
                for attempt in range(100):
                    if process.poll() is not None:raise RuntimeError('Cache restore Server exited')
                    try:
                        with urllib.request.urlopen(cache_url,timeout=1) as response:
                            assert response.read()==avatar_png;break
                    except OSError:time.sleep(.05)
                else:raise RuntimeError('Persisted avatar was unavailable after restart')
                urllib.request.urlopen('http://127.0.0.1/shutdown',timeout=5).close();process.wait(timeout=60)
                if process.returncode:raise RuntimeError('Restored cache Server failed shutdown')
                print('PASS friend avatar restart: cached bytes survive without bootstrap script or working avatar origin')
            if admin and not articles and not friend_avatars:
                # Start the same executable/store again, after removing the bootstrap file.
                source.unlink()
                process = subprocess.Popen([str(binary)], cwd=tmp, stdout=log, stderr=subprocess.STDOUT)
                from admin_smoke_checks import restored
                restored(seen)
                urllib.request.urlopen('http://127.0.0.1/shutdown', timeout=5).close()
                process.wait(timeout=60)
                if process.returncode: raise RuntimeError('Restored Server did not drain')
                print('PASS admin restart: persisted active version restored without bootstrap file')
    finally:
        if process is not None and (process.poll() not in (None, 0) or (not native_only and (not seen or seen[-1] != '/stop'))):
            failure_log=tmp/('startup.log' if '--startup-stress' in sys.argv else 'server.log')
            print('Isolated Server log:', failure_log.read_text(errors='replace')[-4000:])
        if process is not None and process.poll() is None:
            if admin:
                subprocess.run(['ps','-T','-p',str(process.pid),'-o','pid,tid,stat,wchan:32'],check=False)
            process.kill()
            process.wait()
        subprocess.run(['umount', str(resources)], check=True)
        fixture.shutdown()
        fixture.server_close()

"""Checks against the real Server inside server_smoke.py's isolated namespace."""
import concurrent.futures
import json
import time
import urllib.error
import urllib.request

BASE='http://127.0.0.1'
cookie=''
expected_version=''

def request(path, data=None, auth=True, headers=None, expected=200):
    fields={'Content-Type':'application/json','X-Blog-Script-Request':'1'}
    if auth and cookie: fields['Cookie']=cookie
    if headers: fields.update(headers)
    req=urllib.request.Request(BASE+path, data=None if data is None else json.dumps(data).encode(), headers=fields)
    try: response=urllib.request.urlopen(req,timeout=5)
    except urllib.error.HTTPError as error: response=error
    with response:
        body=response.read()
        assert response.status==expected,(path,response.status,expected,body)
        if 'application/json' in response.headers.get('Content-Type',''): return json.loads(body)
        return body

def login():
    global cookie
    req=urllib.request.Request(BASE+'/api/login',data=json.dumps({'username':'smoke','userpassword':'smoke-only'}).encode(),headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=5) as response:
        value=json.loads(response.read());assert value['result']==200,value
        cookie=response.headers['Set-Cookie'].split(';')[0]
        assert cookie.startswith('sessionId='),cookie

def until(predicate, seconds=15):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=predicate()
        if value:return value
        time.sleep(.05)
    raise RuntimeError('Timed out waiting for Server state')

def status():return request('/api/scripts')

def finish(submission, success=True):
    def poll():
        result=request('/api/scripts/jobs/'+submission['jobId'])
        return result if result['state'] in ('succeeded','failed') else None
    job=until(poll)
    assert job['state']==('succeeded' if success else 'failed'),job
    return job

def upload(entry, files):return request('/api/scripts',{'entry':entry,'files':files},expected=202)

def exercise(url,seen):
    global expected_version
    request('/api/scripts',auth=False,expected=401)
    request('/api/scripts',{},auth=False,expected=401)
    login()
    initial=until(lambda: (value if (value:=status())['ready'] and value['current'] else None))
    v1=initial['current'];assert len(v1)==64,initial
    page=request('/background/scripts');assert b'id="script-upload"' in page and b'/assets/' in page
    request('/api/scripts',{},headers={'X-Blog-Script-Request':''},expected=403)
    request('/api/scripts',{},headers={'Origin':'https://other.example'},expected=403)
    request('/api/scripts',{},headers={'Origin':'null'},expected=403)
    request('/api/scripts',{},headers={'Sec-Fetch-Site':'cross-site'},expected=403)
    request('/api/scripts',{'entry':12,'files':{}},expected=400)
    request('/api/scripts',{'entry':'big.js','files':{'big.js':'x'*(128*1024+1)}},expected=400)
    finish(upload('../escape.js',{'../escape.js':'function onStart(){}'}),False)
    finish(upload('bad.mjs',{'bad.mjs':'export function onStart( {'}),False)
    finish(upload('bad.mjs',{'bad.mjs':f"ctx.http.get('{url}/validation-side-effect'); export function onStart(){{}}"}),False)
    assert status()['current']==v1
    files={'main.mjs':"import {request} from './helper.js'; export async function onStart(){await new Promise(resolve=>ctx.after(1000,resolve));await request('v2/start')} export async function onTick(){await request('v2/tick')} export async function onStop(){await request('v2/stop')}",
           'helper.js':f"export async function request(path){{const r=await ctx.http.get('{url}/'+path);if(r.status!==200)throw new Error('HTTP status')}}"}
    begin=time.monotonic();submitted=upload('main.mjs',files)
    assert time.monotonic()-begin<.9,'Upload blocked on async startup'
    warming=status();assert warming['busy'] and warming['current']==v1,warming
    request('/api/scripts/rollback',{},expected=429)
    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as pool:
        results=list(pool.map(lambda _:request('/api/categories'),range(20)))
        assert all(isinstance(json.loads(value) if isinstance(value,bytes) else value,(dict,list)) for value in results),results
    v2=finish(submitted)['version'];assert v2!=v1
    until(lambda:'/v2/tick' in seen)
    assert status()['previous']==v1
    starts=seen.count('/v2/start')
    assert finish(upload('main.mjs',files))['version']==v2
    assert seen.count('/v2/start')==starts and status()['previous']==v1
    finish(upload('bad.mjs',{'bad.mjs':"export function onStart(){throw new Error('startup rejected')}"}),False)
    finish(upload('private.mjs',{'private.mjs':"export async function onStart(){await ctx.http.get('http://127.0.0.1/should-not-connect')}"}),False)
    assert status()['current']==v2
    finish(request('/api/scripts/rollback',{},expected=202))
    state=status();assert state['current']==v1 and state['previous']==v2,state
    assert any(event['error'] for event in state['events']),state
    assert '/validation-side-effect' not in seen
    assert len(state['jobs'])<=32 and len(state['events'])<=64
    request('/api/scripts/jobs/unknown',expected=404)
    expected_version=v1
    print('PASS admin API: authentication, same-origin guard, bounded uploads, module activation, nonblocking startup, 20 concurrent blog requests, failed-update isolation, duplicate uploads, rollback, execution history')

def restored(seen):
    login_ready=False
    last_error=None
    deadline=time.monotonic()+60
    while time.monotonic()<deadline:
        try:login();login_ready=True;break
        except (OSError,ValueError) as error:last_error=repr(error);time.sleep(.1)
    assert login_ready,('Restored Server did not listen',last_error)
    current=until(lambda: (value if (value:=status())['ready'] else None))
    assert current['current']==expected_version,current
    assert current['jobs'][0]['kind']=='restore' and current['jobs'][0]['state']=='succeeded',current
    assert seen.count('/start')>=3,seen

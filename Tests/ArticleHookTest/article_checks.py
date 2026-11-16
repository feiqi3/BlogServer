"""Real Server checks; called inside server_smoke.py's private network/mount namespace."""
import concurrent.futures
import http.client
import json
from pathlib import Path
import sqlite3
import ssl
import statistics
import subprocess
import time
import admin_smoke_checks as admin

def exercise(url,seen,resources,tmp,profiling):
    admin.login();admin.until(lambda:admin.status()['ready'])
    source="""export async function onStart(){}
export async function onTick(){}
export async function onStop(){const c=await ctx.db.categories.get('1');if(c.id!=='1')throw Error('Stop model unavailable');await ctx.http.get('FIXTURE/stop-entered');await new Promise(r=>ctx.after(500,r));await ctx.http.get('FIXTURE/stop')}
export const article={
 async OnSubmit(event,call){
  const c=await call.db.categories.get('1');
  if(c.id!=='1'||!(await call.db.categories.list()).length)throw Error('Model category');
  if(event.operation==='create'&&event.postId!==null)throw Error('Create ID');
  if(event.operation==='update'&&(await call.db.posts.get(event.postId)).id!==event.postId)throw Error('Update ID');
  const title=event.draft.title.trim();
  if(title==='reject')throw Error('Submission rejected');
  if(title==='readonly')await call.db.posts.update('1',{title:'Must not save'});
  if(title==='badpatch')return {id:'999'};
  if(title==='submit-timeout')await new Promise(r=>call.after(11000,r));
  if(title==='cancel-submit'){await call.http.get('FIXTURE/entered-cancel');await new Promise(r=>call.after(1000,r));}
  if(title==='conflict')await new Promise(r=>call.after(300,r));
  const r=await call.http.get('FIXTURE/submit');if(r.status!==200)throw Error('HTTP');
  return {title:(title||'Filled')+'::Saved',profile:event.draft.profile||'Filled profile',content:event.draft.content||'Filled content',titlepic:'fixture.png'};
 },
 async OnRequest(event,call){
  const p=await call.db.posts.get(event.postId);if(p.id!==event.postId)throw Error('Request identity');
  if(p.tags==='fallback')throw Error('Request rejected');
  if(p.tags==='invalid')return {id:'999'};
  if(p.tags==='timeout')await new Promise(r=>call.after(3000,r));
  if(p.tags==='slow')await new Promise(r=>call.after(400,r));
  if(p.tags==='update')await call.db.posts.update(event.postId,{profile:'Model updated'});
  const r=await call.http.get('FIXTURE/request');if(r.status!==200)throw Error('HTTP');
  return {title:'VIEW:'+event.postId,content:'VIEW-CONTENT:'+event.postId};
 }
};
export const submitOnly={OnSubmit:article.OnSubmit};
""".replace('FIXTURE',url)
    admin.finish(admin.upload('article.mjs',{'article.mjs':source}))
    assert 'article.OnSubmit' in admin.status()['functions']
    certificate=ssl.create_default_context(cafile=str(resources/'SSL/cert.pem'))
    def request(path,data=None,tls=False,expected=200):
        connection=http.client.HTTPSConnection('localhost',443,context=certificate,timeout=15) if tls else http.client.HTTPConnection('127.0.0.1',80,timeout=15)
        try:
            connection.request('POST' if data is not None else 'GET',path,body=json.dumps(data) if data is not None else None,headers={'Cookie':admin.cookie,'Content-Type':'application/json'})
            response=connection.getresponse();body=response.read()
            if response.status!=expected:raise AssertionError((path,response.status,body,admin.status().get('events')))
            try:return json.loads(body)
            except ValueError:return body.decode()
        finally:connection.close()
    def draft(title,**fields):return dict(title=title,profile='Profile',content='Stored content',titlepic='fixture.png',category_id='1',script_name='article',tags='',allow_comment=1,allow_rss=0,hide=0,**fields)
    def save(value,tls=False,expected=200):return request('/api/blog',value,tls,expected)
    database=resources/'database/blog.db'
    def rows(sql,*args):
        with sqlite3.connect(database,timeout=10) as connection:return connection.execute(sql,args).fetchall()
    first=save(draft('  First  '));assert first['result']==200,first;first_id=first['id']
    assert rows('SELECT title,content,script_name FROM Posts WHERE id=?',first_id)==[('First::Saved','Stored content','article')]
    assert 'VIEW:'+first_id in request('/post/'+first_id)
    assert rows('SELECT title,content FROM Posts WHERE id=?',first_id)==[('First::Saved','Stored content')]
    editor=request('/api/blog/'+first_id);assert 'VIEW:'+first_id not in json.dumps(editor)
    edited=draft('Edited');edited['id']=first_id;assert save(edited,True)['result']==200
    assert rows('SELECT title FROM Posts WHERE id=?',first_id)==[('Edited::Saved',)]
    filled=draft('');filled.update(profile='',content='',titlepic='');assert save(filled)['result']==200
    for rejected in ('reject','badpatch','readonly'):
        before=rows('SELECT COUNT(*) FROM Posts')[0][0];assert save(draft(rejected),expected=400)['result']==500
        assert rows('SELECT COUNT(*) FROM Posts')[0][0]==before
    timed_out=save(draft('submit-timeout'),expected=400)
    assert 'timed out' in timed_out['msg'].lower(),timed_out
    assert not rows("SELECT id FROM Posts WHERE title LIKE 'submit-timeout%'")
    large=draft('Large');large['content']='x'*(100*1024);large_id=save(large)['id']
    assert rows('SELECT length(content) FROM Posts WHERE id=?',large_id)==[(100*1024,)]
    entered=seen.count('/entered-cancel')
    abandoned=http.client.HTTPConnection('127.0.0.1',80,timeout=5)
    abandoned.request('POST','/api/blog',body=json.dumps(draft('cancel-submit')),headers={'Cookie':admin.cookie,'Content-Type':'application/json'})
    admin.until(lambda:seen.count('/entered-cancel')>entered)
    abandoned.close();time.sleep(1.2)
    assert not rows("SELECT id FROM Posts WHERE title LIKE 'cancel-submit%'")
    for tag in ('fallback','invalid','timeout','update'):
        value=draft('Case-'+tag);value['tags']=tag;id=save(value)['id'];body=request('/post/'+id,tls=True)
        assert ('VIEW:'+id in body)==(tag=='update'),(tag,body[:100])
        if tag=='update':assert rows('SELECT profile FROM Posts WHERE id=?',id)==[('Model updated',)]
    # Concurrent native writer must not be overwritten by a draft prepared earlier.
    conflict=draft('conflict');conflict['id']=first_id
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pending=pool.submit(save,conflict,False,400);time.sleep(.1)
        with sqlite3.connect(database) as connection:connection.execute("UPDATE Posts SET profile='Concurrent edit' WHERE id=?",(first_id,))
        assert 'changed during' in pending.result()['msg']
    assert rows('SELECT profile FROM Posts WHERE id=?',first_id)==[('Concurrent edit',)]
    # Twenty independent saves and visits, with real native DB and outbound HTTP awaits.
    for tls in (False,True):
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as pool:
            ids=[value['id'] for value in pool.map(lambda i:save(draft(f'Concurrent-{tls}-{i}'),tls),range(20))]
            assert len(set(ids))==20
            bodies=list(pool.map(lambda id:request('/post/'+id,tls=tls),ids))
        assert all('VIEW:'+id in body for id,body in zip(ids,bodies))
    # A long admitted invocation survives publication of a newer source version.
    slow=draft('Old');slow['tags']='slow';slow_id=save(slow)['id']
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pending=pool.submit(request,'/post/'+slow_id);time.sleep(.1)
        updated=source.replace("'VIEW:'+event.postId","'NEW:'+event.postId")
        admin.finish(admin.upload('article.mjs',{'article.mjs':updated}))
        assert 'VIEW:'+slow_id in pending.result()
    assert 'NEW:'+slow_id in request('/post/'+slow_id)
    # Exercise actual deferred article controllers over HTTP/2, not just transport fixtures.
    subprocess.run(['node',str(Path(__file__).with_name('http2.mjs')),str(resources/'SSL/cert.pem'),admin.cookie,slow_id],check=True,timeout=35)
    assert not rows("SELECT id FROM Posts WHERE title LIKE 'cancel-submit%'")
    # No hook association: visit must not perform a JS call or an outbound HTTP operation.
    native=draft('Native');native['script_name']='';assert save(native)['result']==200
    native_id=str(rows("SELECT id FROM Posts WHERE title='Native'")[0][0]);before=seen.count('/request')
    timings=[]
    for _ in range(40):
        begin=time.perf_counter();assert 'Native' in request('/post/'+native_id);timings.append((time.perf_counter()-begin)*1000)
    assert seen.count('/request')==before
    state=request('/api/profiling');assert state['enabled']==profiling,state
    if profiling:
        admin.until(lambda:len(request('/api/profiling')['rows'])>2)
        state=request('/api/profiling');assert Path(state['directory']).parent==tmp/'custom-profiles'
        script_rows=[row for row in state['rows'] if row['script']=='blog' and row['hook']=='article.OnRequest']
        assert len(script_rows)>=2 and any(row['errors'] for row in script_rows),script_rows
        assert all(row['averageUs']<=row['maxUs'] and row['calls']>0 for row in state['rows'])
        assert any(row['name']=='Article.Render' for row in state['rows'])
    else:assert not (tmp/'custom-profiles').exists()
    print(f'PASS article hooks: pre-save transformation/rejection, model reads/writes, optimistic conflicts, fallback, hot reload, 20 concurrent HTTP/1.1 + HTTPS requests; profiling={profiling}')
    print(f'MEASURE native article, 40 new HTTP connections: mean={statistics.mean(timings):.3f}ms max={max(timings):.3f}ms (local isolated Debug; no release baseline comparison)')

def native_exercise(resources):
    admin.request('/api/profiling',auth=False,expected=401)
    admin.login()
    value=admin.request('/api/blog',{'title':'Native-only','content':'content','profile':'profile','titlepic':'fixture.png','category_id':'1'})
    if isinstance(value,bytes):value=json.loads(value)
    assert value['result']==200,value
    with sqlite3.connect(resources/'database/blog.db') as connection:id=str(connection.execute('SELECT id FROM Posts').fetchone()[0])
    assert b'Native-only' in admin.request('/post/'+id)
    state=admin.until(lambda:(value if (value:=admin.request('/api/profiling'))['rows'] else None))
    assert state['enabled'] and any(row['name']=='Article.Render' for row in state['rows'])
    admin.request('/api/scripts',expected=501)
    print('PASS script-disabled Server: native submit/render, profiler enabled independently, authenticated generic status')

def shutdown_probe(seen,previous):
    admin.until(lambda:seen.count('/stop-entered')>previous,seconds=30)
    result=admin.request('/api/blog',{'title':'Must not save during stop','category_id':'1','profile':'profile','content':'content','titlepic':'fixture.png','script_name':'article'},expected=503)
    assert result['result']==500
    print('PASS draining Server: onStop retains model access; new article writes receive 503 instead of bypassing preprocessing')

def persisted(tmp,enabled,require_script=True):
    if not enabled:
        assert not (tmp/'custom-profiles').exists();print('PASS configuration: disabled profiler creates no output directory');return
    runs=list((tmp/'custom-profiles').glob('run-*'));assert len(runs)==1,runs
    summary=json.loads((runs[0]/'summary.json').read_text());assert summary['rows'] and not summary['enabled'],summary
    samples=[json.loads(line) for line in (runs[0]/'samples.jsonl').read_text().splitlines()]
    assert samples and all(sample['id'] and sample['elapsedUs']>=0 for sample in samples)
    if require_script:assert any(row['sourceVersion'] for row in summary['rows'] if row['script']=='blog')
    if require_script:assert any(row['hook']=='stop' and row['calls']>0 and row['errors']==0 for row in summary['rows'] if row['script']=='blog')
    assert not summary['writeError']
    print('PASS configuration: custom output directory, timing, samples, summary, shutdown flush'+(' and script version metadata' if require_script else ''))

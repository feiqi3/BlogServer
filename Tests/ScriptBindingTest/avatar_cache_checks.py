"""Real script/cache integration; fixtures live in a private mount/network namespace."""
import base64
import concurrent.futures
import http.client
import json
from pathlib import Path
import ssl
import subprocess
import urllib.parse
import urllib.request
import urllib.error
import admin_smoke_checks as admin

def exercise(url,seen,resources,state,png):
    admin.login();admin.until(lambda:admin.status()['ready'])
    module=(Path(__file__).resolve().parents[2]/'Server/Scripts/FriendLinks/main.mjs').read_text()
    wrapper="""import * as job from './friends.mjs';
export const friends=job.friends;
export async function onStart(){await job.onStart()}
export async function onTick(){await job.onTick()}
export async function onStop(){job.onStop();await ctx.http.get('FIXTURE/stop')}
export const inspect={async OnSubmit(event,call){
 const input=JSON.parse(event.draft.content);let value;
 try{value=input.data===undefined?await call.avatarCache.get(input.url):await call.avatarCache.store(input.url,input.data)}
 catch(error){value={error:String(error)}}
 return {profile:JSON.stringify(value)};
}};
""".replace('FIXTURE',url)
    def upload(suffix=''):
        admin.finish(admin.upload('main.mjs',{'main.mjs':wrapper+suffix,'friends.mjs':module}))
    upload()
    def draft(title,content,group='friends'):
        return {'title':title,'content':content,'profile':'Fixture','titlepic':'fixture.png','category_id':'1',
                'allow_comment':0,'allow_rss':0,'hide':0,'script_name':group}
    def save(value):
        result=admin.request('/api/blog',value);assert result['result']==200,result;return result['id']
    def inspect(source,data=None):
        value={'url':source}
        if data is not None:value['data']=data
        post=save(draft('Storage inspection',json.dumps(value),'inspect'))
        record=admin.request('/api/blog/'+post)
        if isinstance(record,bytes):record=json.loads(record)
        return json.loads(record['profile'])
    def fetch(source,tls=False,headers=None):
        path='/avatar-cache?source='+urllib.parse.quote(source,safe='')
        request=urllib.request.Request(('https' if tls else 'http')+'://127.0.0.1'+path,headers=headers or {})
        try:response=urllib.request.urlopen(request,context=ssl._create_unverified_context() if tls else None,timeout=5)
        except urllib.error.HTTPError as error:response=error
        with response:return response.status,response.headers,response.read()
    avatar=url+'/friend/avatar.png?a=1&b=two%20words'
    content=f'```links\n[One](https://example.org) | {avatar} | Description\n[Duplicate](https://example.net) | {avatar} | Other\n```'
    post=save(draft('友链',content))
    admin.until(lambda:fetch(avatar)[0]==200)
    assert fetch(avatar)[2]==png
    assert seen.count('/friend/avatar.png?a=1&b=two%20words')==1,'No URL deduplication'
    assert fetch(url+'/never-cached')[0]==404
    before=len(seen)
    for _ in range(5):admin.request('/links')
    assert len(seen)==before,'Visitor caused a download'
    status,headers,body=fetch(avatar,True)
    assert status==200 and body==png and headers['Content-Type'].startswith('image/png')
    assert headers['X-Content-Type-Options']=='nosniff'
    assert fetch(avatar,True,{'If-None-Match':headers['ETag']})[0]==304
    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
        assert all(result[2]==png for result in executor.map(lambda _:fetch(avatar),range(20)))
    binary=base64.b64encode(png).decode()
    maximum=base64.b64encode(png+b'\0'*(128*1024-len(png))).decode()
    large=url+'/large-avatar';assert inspect(large,maximum)['size']==128*1024
    assert len(fetch(large)[2])==128*1024,'128 KiB bridge lost bytes'
    assert inspect(avatar)['size']==len(png)
    for invalid in ('!', 'AAAA=',base64.b64encode(b'<html>bad</html>').decode(),base64.b64encode(b'\0'*(128*1024+1)).decode()):
        assert 'error' in inspect(avatar,invalid)
        assert fetch(avatar)[2]==png,'Invalid refresh damaged cached image'
    assert 'error' in inspect('../outside',binary)
    svg=url+'/svg';image=b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"><path d="M0 0h1v1H0"/></svg>'
    assert inspect(svg,base64.b64encode(image).decode())['type']=='image/svg+xml'
    assert fetch(svg)[2]==image and 'sandbox' in fetch(svg)[1]['Content-Security-Policy']
    for malicious in (b'<svg><script>alert(1)</script></svg>',b'<!DOCTYPE svg><svg/>'):
        assert 'error' in inspect(svg,base64.b64encode(malicious).decode())
        assert fetch(svg)[2]==image
    # Expire only this temporary fixture. A failed periodic refresh must keep it.
    cache=resources/'cache/avatars'
    for record in cache.glob('*.avatar'):
        fields,body=record.read_bytes().split(b'\n',1)
        metadata=json.loads(fields);metadata['cachedAt']=0
        record.write_bytes(json.dumps(metadata).encode()+b'\n'+body)
    state['fail']=True;before=seen.count('/friend/avatar.png?a=1&b=two%20words')
    upload('\n// restart refresh state\n')
    admin.until(lambda:seen.count('/friend/avatar.png?a=1&b=two%20words')>before)
    assert fetch(avatar)[2]==png,'Network failure removed last successful avatar'
    assert inspect(avatar)['cachedAt']==0
    state['fail']=False
    admin.until(lambda:inspect(avatar)['cachedAt']>0)
    assert fetch(avatar)[2]==png
    assert not list(cache.glob('*.tmp'))
    # The public cache route serves HTTP/2 independently of article execution.
    script=Path(__file__).with_name('avatar_http2.mjs')
    subprocess.run(['node',str(script),avatar,base64.b64encode(png).decode()],check=True,timeout=15)
    article=admin.request('/api/blog/'+post)
    if isinstance(article,bytes):article=json.loads(article)
    assert article['content']==content,'Cache rewrote the article'
    print('PASS friend avatars: nonblocking submit, deduplication, 128 KiB binary bridge, HTTP/HTTPS/H2, SVG policy, ETag, concurrent reads, atomic replacement, stale-on-error and periodic recovery')

import http2 from 'node:http2';
import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const client=http2.connect('https://127.0.0.1',{ca:readFileSync(process.argv[2]),servername:'localhost'});
const cookie=process.argv[3],existing=process.argv[4];
async function request(path,data){
    return new Promise((resolve,reject)=>{
        const body=data?JSON.stringify(data):null;
        const stream=client.request({':method':body?'POST':'GET',':path':path,cookie,'content-type':'application/json',...(body?{'content-length':Buffer.byteLength(body)}:{})});
        let status,text='';stream.on('response',headers=>status=headers[':status']);stream.setEncoding('utf8');stream.on('data',chunk=>text+=chunk);stream.on('error',reject);
        stream.on('end',()=>{try{assert.equal(status,200,text);resolve(text);}catch(error){reject(error);}});stream.end(body);
    });
}
try{
    const results=await Promise.all(Array.from({length:20},(_,i)=>request('/api/blog',{title:`HTTP2-${i}`,profile:'profile',content:'content',titlepic:'fixture.png',category_id:'1',script_name:'article',tags:''})));
    const ids=results.map(text=>{const value=JSON.parse(text);assert.equal(value.result,200,text);return value.id;});assert.equal(new Set(ids).size,20);
    const pages=await Promise.all(ids.map(id=>request('/post/'+id)));pages.forEach((body,i)=>assert.ok(body.includes('NEW:'+ids[i])));
    const reset=client.request({':path':'/post/'+existing});reset.on('error',()=>{});setTimeout(()=>reset.close(http2.constants.NGHTTP2_CANCEL),30);
    const cancelledBody=JSON.stringify({title:'cancel-submit',profile:'profile',content:'content',titlepic:'fixture.png',category_id:'1',script_name:'article'});
    const cancelled=client.request({':path':'/api/blog',':method':'POST',cookie,'content-type':'application/json','content-length':Buffer.byteLength(cancelledBody)});
    cancelled.on('error',()=>{});cancelled.end(cancelledBody);setTimeout(()=>cancelled.close(http2.constants.NGHTTP2_CANCEL),100);
    assert.ok(await request('/api/categories'));await new Promise(resolve=>setTimeout(resolve,1300));
    console.log('PASS actual article HTTP/2 over TLS: 20 concurrent saves + visits, response identity, reset/late completion, cancelled preprocessing and native route availability');
}finally{client.close();}

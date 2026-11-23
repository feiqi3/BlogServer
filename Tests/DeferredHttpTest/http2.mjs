import http2 from 'node:http2';
import fs from 'node:fs';
import assert from 'node:assert/strict';
const session=http2.connect(`https://127.0.0.1:${process.argv[2]}`,{servername:'localhost',ca:fs.readFileSync(process.argv[3])});
const errors=[];session.on('error',error=>errors.push(error));
async function request(path){return await new Promise((resolve,reject)=>{
    const stream=session.request({':path':path});let body='',headers;
    stream.on('response',value=>{headers=value;});stream.on('data',chunk=>{body+=chunk;});
    stream.on('end',()=>resolve({body,headers}));stream.on('error',reject);stream.end();
});}
try{
    const unavailable=await request('/unavailable/test');assert.equal(unavailable.headers[':status'],503);assert.equal(unavailable.body,'unavailable');
    const requests=Array.from({length:20},(_,i)=>request(`/later/h2-${i}`));
    const fast=await request('/fast/independent');assert.equal(fast.body,'independent');
    const values=await Promise.all(requests);
    values.forEach((value,i)=>{assert.equal(value.body,`h2-${i}`);assert.equal(value.headers['x-finalized'],'once');});
    const timeout=await request('/timeout/late');assert.equal(timeout.headers[':status'],408);
    const reset=session.request({':path':'/later/reset'});reset.on('error',()=>{});reset.end();
    await new Promise(resolve=>setTimeout(resolve,30));reset.close(http2.constants.NGHTTP2_CANCEL);
    await new Promise(resolve=>setTimeout(resolve,250));
    assert.equal((await request('/fast/after-reset')).body,'after-reset');
    assert.deepEqual(errors,[]);
    console.log('PASS HTTP/2: 20 streams, identity, finalization once, timeout, stream reset and late completion');
}finally{session.close();}

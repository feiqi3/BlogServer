import assert from 'node:assert/strict';
import http2 from 'node:http2';
const session=http2.connect('https://127.0.0.1',{rejectUnauthorized:false});
try {
    const request=session.request({':path':'/avatar-cache?source='+encodeURIComponent(process.argv[2])});
    let headers;request.on('response',value=>{headers=value;});
    const chunks=[];for await(const chunk of request)chunks.push(chunk);
    assert.equal(headers[':status'],200);
    assert.equal(Buffer.concat(chunks).toString('base64'),process.argv[3]);
    assert.equal(headers['x-content-type-options'],'nosniff');
    const cached=session.request({':path':'/avatar-cache?source='+encodeURIComponent(process.argv[2]),'if-none-match':headers.etag});
    let status;cached.on('response',value=>{status=value[':status'];});
    const empty=[];for await(const chunk of cached)empty.push(chunk);
    assert.equal(status,304);assert.equal(Buffer.concat(empty).length,0);
    console.log('PASS HTTP/2 cached avatar bytes and response headers');
} finally {session.close();}

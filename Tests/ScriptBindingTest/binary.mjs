let late = 0;
let retained = [];
const binary = {responseType: 'arrayBuffer', maxBytes: 1024};
function check(value, message) { if (!value) throw Error(message); }
function header(response, name) {
    return response.headers.find(([key]) => key.toLowerCase() === name)?.[1];
}
function bytes(response, index = 0) {
    check(response.body instanceof ArrayBuffer, 'Body is not an ArrayBuffer');
    const data = new Uint8Array(response.body);
    check(data.length === 512, 'Binary length changed');
    for (let i = 4; i < data.length; ++i) check(data[i] === i % 256, 'Binary byte changed');
    check(new DataView(response.body).getUint32(0, true) === index, 'Request identity changed');
    return data;
}
export async function fetch(input, call) {
    const result = await call.http.get(input.url, binary);
    bytes(result, input.index || 0);
    check(result.status === 200, 'Wrong status');
    check(header(result, 'content-type') === 'application/octet-stream', 'Headers missing');
    return {index: input.index || 0, length: result.body.byteLength};
}
export async function range(input, call) {
    const result = await call.http.get(input.url + '/range', {
        responseType: 'arrayBuffer', maxBytes: 32,
        headers: {Range: 'bytes=0-31', 'X-Probe': 'accepted'}
    });
    check(result.status === 206 && result.body.byteLength === 32, 'Range failed');
    check(header(result, 'content-range') === 'bytes 0-31/512', 'Content-Range missing');
    check(header(result, 'x-probe') === 'accepted', 'Request headers missing');
    return true;
}
export async function legacy(input, call) {
    const result = await call.http.get(input.url + '/text');
    check(result.status === 200 && result.body === 'hello', 'Legacy text changed');
    const explicit = await ctx.http.get(input.url + '/text', {responseType: 'text'});
    check(explicit.body === 'hello', 'Explicit text failed');
    return true;
}
export async function metadata(input, call) {
    const empty = await call.http.get(input.url + '/empty', binary);
    check(empty.status === 204 && empty.body.byteLength === 0, 'Empty body failed');
    const missing = await call.http.get(input.url + '/missing', binary);
    check(missing.status === 404 && new Uint8Array(missing.body)[0] === 255, 'Non-success body changed');
    const redirect = await call.http.get(input.url + '/redirect', binary);
    check(redirect.status === 302 && header(redirect, 'location') === '/bytes', 'Redirect was followed or lost');
    const repeated = await call.http.get(input.url + '/headers', binary);
    check(repeated.headers.filter(([name]) => name.toLowerCase() === 'x-repeat').length === 2, 'Duplicate headers lost');
    return true;
}
export async function invalid(input, call) {
    const cases = [null, [], {responseType:'blob'}, {responseType:1}, {maxBytes:0},
        {maxBytes:4194305}, {maxBytes:NaN}, {maxBytes:1.5}, {maxBytes:'1'},
        {timeoutMs:0}, {timeoutMs:Infinity}, {timeoutMs:30001}, {unknown:true},
        {headers:[]}, {headers:{Host:'other.example'}}, {headers:{'Content-Length':'1'}},
        {headers:{'Transfer-Encoding':'chunked'}}, {headers:{Connection:'close'}},
        {headers:{Upgrade:'websocket'}}, {headers:{'Bad Header':'x'}},
        {headers:{'X-Test':'a\r\nb'}}, {headers:{'X-Test':'a\0b'}},
        {headers:{'X-Test':1}}, {headers:{'X-Test':'x'.repeat(8193)}},
        {headers:Object.fromEntries(Array.from({length:33},(_,i)=>['X-'+i,'v']))},
        {get maxBytes(){throw Error('getter sentinel')}},
        {headers:{get value(){throw Error('getter sentinel')}}}];
    let count = 0;
    for (const options of cases) {
        try { call.http.get(input.url + '/invalid-never', options); }
        catch (error) { count++; }
    }
    check(count === cases.length, 'Invalid options were not synchronously rejected');
    return count;
}
export async function limits(input, call) {
    for (const route of ['/oversize', '/oversize-chunked', '/truncated']) {
        let failed = false;
        try { await call.http.get(input.url + route, {responseType:'arrayBuffer', maxBytes:1024}); }
        catch (error) { failed = true; }
        check(failed, 'Invalid body was accepted');
    }
    let timed = false;
    try { await call.http.get(input.url + '/slow', {...binary, timeoutMs:30}); }
    catch (error) { timed = true; }
    check(timed, 'HTTP deadline ignored');
    bytes(await call.http.get(input.url + '/bytes', binary));
    return true;
}
export async function quota(input, call) {
    const options = {responseType:'arrayBuffer',maxBytes:4194304};
    const first = call.http.get(input.url + '/hold', options);
    const second = call.http.get(input.url + '/hold', options);
    let rejected = false;
    try { call.http.get(input.url + '/quota-never', options); }
    catch (error) { rejected = true; }
    check(rejected, 'In-flight byte budget ignored');
    await Promise.all([first,second]);
    bytes(await call.http.get(input.url + '/bytes', options));
    return true;
}
export async function memory(input, call) {
    retained = [];
    for (let i=0;i<4;i++) retained.push((await call.http.get(input.url + '/large?size=1048576', {responseType:'arrayBuffer'})).body);
    let rejected = false;
    try { await call.http.get(input.url + '/large?size=4194304', {responseType:'arrayBuffer',maxBytes:4194304}); }
    catch (error) { rejected = true; }
    check(rejected, 'ArrayBuffer did not count against the JS heap');
    retained = [];
    bytes(await call.http.get(input.url + '/bytes', binary));
    return true;
}
export async function slow(input, call) {
    const result = await call.http.get(input.url + '/slow', {responseType:'arrayBuffer',maxBytes:4194304});
    late++;return result.body.byteLength;
}
export function stats(){return {late};}
export async function delayed(input, call) {
    const result = await call.http.get(input.url + '/bytes', binary);
    await new Promise(resolve=>call.after(200,resolve));
    bytes(result);return 'retired buffer intact';
}
export async function keep(input, call) {
    retained.push((await ctx.http.get(input.url + '/bytes', binary)).body);
    return true;
}

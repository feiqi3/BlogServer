# Script HTTP

`ctx.http.get(url, options?)` and invocation-scoped `call.http.get` use the same
FeiLib asynchronous client. The default body remains a UTF-8 string. Binary mode
returns an `ArrayBuffer`; no base64 or JSON conversion is involved.

```js
export async function inspect(input, call) {
    const response = await call.http.get(input.url, {
        responseType: 'arrayBuffer',
        headers: {Range: 'bytes=0-65535'},
        maxBytes: 65536,
        timeoutMs: 5000
    });
    if (response.status !== 200 && response.status !== 206)
        throw Error('Image request failed');
    const bytes = new Uint8Array(response.body);
    return {length: bytes.byteLength, prefix: Array.from(bytes.subarray(0, 8))};
}
```

Options:

| Field | Default | Accepted values |
| --- | --- | --- |
| `responseType` | `'text'` | `'text'`, `'arrayBuffer'` |
| `timeoutMs` | `5000` | Integer, 1..30000 ms |
| `maxBytes` | `1048576` | Integer, 1..4194304 bytes |
| `headers` | `{}` | String-valued object; at most 32 fields and 8192 encoded bytes |

Unknown options, invalid limits and invalid headers throw synchronously, before
network admission. Host, Content-Length, Transfer-Encoding, Connection and Upgrade
are client-managed. The client supplies `Accept-Encoding: identity` by default.

Responses contain `status`, `body`, and `headers`. Headers are `[name, value]`
pairs with original spelling and duplicate entries preserved:

```js
const contentRange = response.headers.find(
    ([name]) => name.toLowerCase() === 'content-range'
)?.[1];
```

Non-2xx statuses still return their bodies; check `status` in the script. Redirects
are returned without following them. Transfer framing is decoded, but content
encoding such as gzip is not decompressed. TLS verification and the host's
immutable address policy apply equally to text and binary requests.
This extends the existing HTTP/1.1 GET client; it does not add binary uploads.

The per-runtime binary admission budget is 8 MiB, reserved using each pending
request's `maxBytes`. It is released on native completion, including cancellation.
Lower `maxBytes` for small probes and large concurrent batches. An exhausted
admission budget throws synchronously; network errors reject the returned Promise.
The body uses one copy into QuickJS-managed storage, counted against the configured
`FScriptOptions::memoryLimitBytes` heap cap (8 MiB by default). Retained buffers
remain valid independently of the native response. A body
allocation failure rejects the fetch; catching it and releasing retained data can
recover space. Uncaught memory faults retain the existing runtime-failure policy.

`call.http` work is cancelled when its invocation ends. Retained buffers remain
valid while their version drains, and are freed with normal JS ownership. Native
invocation inputs/results remain JSON with the host's existing size budget;
return parsed metadata rather than the binary body to C++. This API receives
binary GET responses; it does not add streaming.

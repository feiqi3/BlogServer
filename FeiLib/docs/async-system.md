# Asynchronous HTTP and jobs

`FHttpClient` runs network I/O on an existing `FEventLoop`. `JobPool` runs work on worker threads and posts results to a chosen loop. `JobGraph` chains these operations; nothing runs until `dispatch()`.

```cpp
#include "Async/FJobGraph.h"

Fei::Http::FHttpClient client(loop);
Fei::Async::JobPool workers;
Fei::Http::ClientRequest request;
request.url = "https://example.com/value";

auto handle = Fei::Async::JobGraph::begin(loop)
    .thenHttp(client, request)
    .thenJob(workers, [](const Fei::Http::ClientResponse& response) {
        if (response.status != 200) throw std::runtime_error("HTTP error");
        return std::stoi(response.body) * 2;
    })
    .onFinish([](const int& value) { /* use value */ })
    .onError([](const Fei::Async::Error& error) { /* handle failure */ })
    .dispatch(std::chrono::seconds(30));
// handle.cancel();
```

`thenHttp`, `thenJob`, and `thenAsync<T>` connect asynchronous steps; `then` performs a short nonblocking transform on the graph loop. Completion and error handlers run on that loop. A graph is a sequential chain, not a parallel DAG or a complete Promise implementation. Dropping a handle does not cancel it; `done()` indicates a terminal result, but its callback may still be queued.

`FHttpClient::submit` can also be used directly. Its callbacks run on `client.loop()`. HTTPS verifies the certificate and hostname. HTTP error statuses are returned as responses; redirects, retries, decompression, HTTP/2, and early responses during large uploads are not handled automatically. Requests have configurable time and body limits.

Keep the loop, client, and pool alive until completion callbacks have run. To shut down, stop new work, cancel or wait for graphs and their callbacks, shut down the worker pool and drain its completion queue, then call `client.shutdown(onDrained)` and wait for `onDrained` before stopping the loop. Do not block the loop thread while waiting. Native DNS may delay shutdown until resolution returns.

For server-owned background I/O, obtain `server.jobLoop()` before `server.run()`. The TCP server starts and joins this loop; drain its clients before stopping the server.

`ClientOptions::allowAddress` optionally checks the hostname and each numeric
DNS result on the client loop before a socket is opened. Every result must
pass; a rejection or exception returns `ErrorCode::AccessDenied`. Use an
immutable policy for a client's lifetime; pooled connections retain their
original connection decision. An empty policy preserves unrestricted access.

To run the isolated async tests (C++20, OpenSSL, Python, and the `openssl` command are required):

```bash
cmake -S Tests/AsyncTest -B build/async-validation
cmake --build build/async-validation --parallel 2
ctest --test-dir build/async-validation --output-on-failure
```

# Deferred HTTP responses

Ordinary controllers still return `FHttpResponse`. A deferred response starts
async work after routing and completes through a copyable responder:

```cpp
return Fei::Http::FHttpResponse::defer([&pool, &jobLoop](auto reply) {
    auto operation = pool.submit([](Fei::Async::CancellationToken cancelled) -> std::any {
        if (cancelled.cancelled()) return std::string();
        return std::string("result"); // perform bounded work here
    }, [reply](auto result) {
        Fei::Http::FHttpResponse response;
        if (result.ok()) response.setBody(std::any_cast<std::string>(*result.value));
        else response.setStatusCode(Fei::Http::StatusCode::_500).setBody("Work failed");
        reply.complete(std::move(response));
    }, &jobLoop);
    reply.onCancel([operation] { operation.cancel(); });
}, std::chrono::seconds(5));
```

Include `Http/FHttpResponse.h` and `Async/FJobPool.h`. Configure `jobLoop` before
server startup and keep the pool and loop alive until work has completed.

The starter runs on the connection loop and must return promptly. `complete`
may run on another thread; finalization and sending return to the connection
loop. The first completion is accepted; duplicates and late completions return
false. A nested deferred completion becomes HTTP 500.

Register cancellation in the starter. Disconnect, stream reset, timeout and
server shutdown cancel pending work. A cancellation callback registered after
cancellation may run inline. Callbacks must not block. Cancellation does not
roll back external effects or database writes that already committed.

Timeout defaults to 30 seconds; valid values are positive and at most 5 minutes.
Timeout returns HTTP 408. Starter exceptions return HTTP 500. Pending admission
is bounded by `HttpMaxDeferredRequests` (default 256, range 1..65536); excess
requests receive HTTP 429 without starting work.

HTTP/1.1 preserves response order and retains at most 256 KiB of later request
bytes while a response waits. Exceeding that buffer closes the connection.
HTTP/2 associates each completion with its original stream and allows unrelated
streams to complete independently. Final response callbacks run once.

Synchronous responses allocate no deferred request state. The server must be
stopped from a control thread so pending work is cancelled before loops exit.
These APIs are generic; blog hooks and ORM policy belong to Server.

# Profiling

Profiling is independent of scripting. It records inclusive wall time, counts,
errors, averages and maxima. Nested scopes include their children; asynchronous
spans include waiting time. These measurements are not CPU time.

```cpp
#include "Profiling/FProfiler.h"

Fei::FProfiler::start({}, [](const Fei::FProfileBatch& batch) {
    // Collector thread: persist batch.samples and batch.summary here.
});

void render() {
    FeiScope("Article.Render");
    // Automatic completion, including exception unwinding.
}

// Control thread, after application work has drained:
Fei::FProfiler::stop();
```

Use a literal for `FeiScope`; each call site has a cached numeric ID. Label
registration copies strings once. The warmed recording path does no string
formatting, heap allocation, file I/O or global mutex locking. The first event
on each thread allocates its buffer. Disabled scopes skip registration and
clock reads. `-DFEILIB_ENABLE_PROFILING=OFF` compiles both scope macros out,
without evaluating their arguments.

Register dynamic names during setup and use the returned ID:

```cpp
Fei::FProfileLabel label;
label.name = "Image.Fetch";
const auto fetchId = Fei::FProfiler::registerLabel(std::move(label));

auto span = Fei::FProfileSpan::begin(fetchId);
startAsync([span = std::move(span)](auto result) mutable {
    span.finish(!result.ok()); // Completion may run on another thread.
});
```

Spans are move-only and finish at most once. Destruction finishes an unfinished
span successfully; explicitly call `finish(true)` for failure or cancellation.
For copyable `std::function` callbacks, capture a shared span:

```cpp
auto span = std::make_shared<Fei::FProfileSpan>(Fei::FProfileSpan::begin(fetchId));
client.submit(request, [span](auto result) {
    span->finish(!result.ok());
});
```

This wrapper allocates once per asynchronous operation; ordinary scopes and
the underlying warmed record operation do not allocate. To avoid that wrapper,
store the span in the operation's existing context and capture that context.
`FeiScopeId(fetchId)` provides a synchronous scope using the same ID. An existing
timer can submit `FProfiler::record(fetchId, elapsed, failed)` directly. IDs are
process-local; old spans cannot record into a restarted profiling session.

The collector drains 1,024 fixed-size records per thread every 100 ms by default.
The default budget is 128 concurrent buffers; exited threads are reclaimed after
draining. The label registry holds at most 4,096 labels for the process lifetime.
Full buffers drop samples; excess threads and labels are reported separately.
Aggregates cover accepted samples, so check all drop counters before interpreting
them. Stop drains queued records; in-flight scopes must finish before stop to be
included. A sink exception is reported with a failed-sample count and does not
terminate the application. Sink callbacks must not call start, flush or stop.

`snapshot()` returns the latest collected aggregates without file I/O. `flush()`
requests a collection pass and waits for its sink; use it only on a control
thread. Stop and join application producers before changing profiler sessions.

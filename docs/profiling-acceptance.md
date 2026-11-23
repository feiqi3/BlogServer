# Profiling validation

Local Linux/WSL validation; no production deployment. All database tests use
schema-only temporary fixtures in private mount/network namespaces.

| Check | Evidence |
| --- | --- |
| Literal scopes, nesting, exceptions, moved async spans, exact 20-thread aggregates | `Tests/ProfilingTest/test.cpp` |
| No producer-thread allocations after warmup; disabled scopes allocate nothing | Allocation counters in the same test |
| Buffer/thread/label bounds, rejected-label cache, thread-exit reclamation, restart isolation, stop race, sink failures | Same standalone tests |
| Instrumentation compiled out; macro arguments not evaluated | `FeiLib.ProfilingCompiledOut` |
| Average/max, source versions, samples, summary, final flush, observable file errors | `Tests/ArticleHookTest/profiler.py` |
| Enabled/disabled configuration and custom output path in actual Server | `server_smoke.py --articles [--profiling]` |
| Profiling independent of QuickJS | Script-disabled Server with `--native-only --articles --profiling` |
| Script/native display, group metadata, safe output, upload/rollback, mobile layout | `blogweb/scripts/check-script-admin.mjs` |

Standalone commands from the repository root:

```sh
python3 Tests/ProfilingTest/run.py . build/profiling-test
cmake -S Tests/ProfilingTest -B build/profiling-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/profiling-cmake -j4
ctest --test-dir build/profiling-cmake --output-on-failure
```

One optimized local run measured about 1.3 ns per disabled scope and 112 ns per
enabled, accepted scope. The enabled measurement uses 50,000 warmed calls with
flush time excluded; the separate million-call overload check verifies drops.
These are instrumentation microbenchmarks, not release Server throughput.

Article tests additionally cover HTTP/1.1, HTTPS/1.1 and HTTP/2 over TLS with
20 concurrent saves and visits, script/model waits, rejection, timeout, fallback,
optimistic conflicts, hot reload and stream reset. Unconfigured visits perform
no script HTTP calls. No zero-overhead production latency claim is made.
Lifecycle regressions check asynchronous `onStop` timing and failures, model
reads during stop, and HTTP 503 for article submissions while draining.
Browser tests also cover loading guards, stale responses, retained script
associations, duplicate-save prevention and rejected preprocessing drafts.

Startup stress exposed a pre-existing `FTcpServer::run` race: worker threads
read the Loop vector while the control thread grew it. A diagnostic stack
resolved to that startup lambda. Loop construction now finishes before threads
start, and thread lambdas capture stable Loop pointers. The regression command
is `server_smoke.py ... --articles --profiling --startup-stress` (100 startups).

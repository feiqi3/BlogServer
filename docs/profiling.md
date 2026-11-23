# Server profiling

Add settings to the active environment in `Server/resources/config/server.cfg`:

```text
ProfilingEnabled: true
ProfilingDirectory: /data/blog/profiling
```

Restart the server to apply changes. Profiling is disabled by default. When
disabled, it creates no collector or output directory. The default enabled
output location is `Server/resources/profiling`. Use an absolute path outside
the public web directory; relative paths use the process working directory.
The service account needs write permission. A startup
failure is logged and leaves profiling disabled without stopping the blog.

The Server adapter uses FeiLib's single collector thread for batch file output:

- `run-<timestamp>/samples.jsonl`: numeric ID, completion thread, relative end
  time, microsecond duration and success for each accepted sample.
- `summary.json`: ID-to-label metadata, counts, errors, average and maximum.

Samples rotate after 8 MiB into three historical files. Each restart creates a
new run folder; old runs require operator retention/cleanup. This is bounded
diagnostic history, not a durable audit log. Disk errors and dropped samples are
visible in status. Shutdown flushes queued records before closing the collector.

Authenticated `GET /api/profiling` returns current-run statistics, including when
scripting is disabled. The script administration page also displays the top 100
items by maximum duration. Statistics refresh after collection, normally within
100 ms plus output time. Opening status does not read files.

```cpp
#include "Profiling/FProfiler.h"
void process() {
    FeiScope("Script");
    // Measure a native synchronous scope.
}
```

Script hook IDs are registered on package load and cached per runtime version.
Each invocation submits its existing timer and numeric ID without building a
profiling string. Source version hashes are attached as metadata. Profiling a
script includes its awaited HTTP/database work; `Article.Render` measures native
rendering. Nested scope totals overlap and should not be summed as CPU time.
Lifecycle `start`, `tick` and `stop` are recorded separately. Stop timing begins
when `onStop` starts, after admitted work drains, and includes asynchronous waits.
Stop failures are recorded and cleanup continues. Model workers remain available
until script shutdown completes.
See [FeiLib profiling](../FeiLib/docs/profiling.md) for async spans and limits.
Validation details are in [Profiling validation](profiling-acceptance.md).

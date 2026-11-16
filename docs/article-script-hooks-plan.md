# Article hook implementation

Local implementation on `ScriptEngine`; no production deployment or database
replacement. Usage is documented in
[Script administration](../Server/ScriptRuntime/README.md).

## Boundaries

- FeiLib: deferred HTTP completion, cancellation, async native Promise bindings,
  version-pinned invocations and generic profiling.
- Server: article associations, model policy, pre-save transformations,
  presentation patches, native rendering fallback and profiling persistence.
- Frontend: select an exported hook group in the article editor; manage the
  shared script package and inspect performance statistics in administration.

An empty `script_name` keeps the native path without additional SQL, JS,
runtime creation or worker dispatch. Groups reuse the active `blog` package;
no runtime is created per visitor. New invocations use the new version after
publication, while admitted calls finish on their original runtime.

`OnSubmit` runs before a short optimistic save transaction. It may read models
but must return changes as a patch; rejection or timeout does not save the draft.
`OnRequest` patches a copy before rendering; script/patch failures use the stored
article. Native render/capacity failures are reported separately. IDs are decimal
strings. Invocation JSON is bounded at 256 KiB.

Model jobs use two bounded FeiLib workers with private-cache SQLite connections,
WAL and the existing busy timeout. Native connection behavior is preserved.
Worker exit closes connections and finalizes cached statements.

## Validation

- Actual Server: HTTP/1.1, HTTPS/1.1 and HTTP/2 over TLS; 20 concurrent saves and
  visits per transport, identity, stream reset, hot updates, model reads/writes,
  rejected/timed-out preprocessing, optimistic conflicts and rendering fallback.
  Disconnects and stream resets during preprocessing must not save the draft.
- Profiling enabled and disabled: custom output directory, per-version script
  statistics, native scopes, samples, summary and shutdown flush.
- Script-disabled Server: native submit/render and independently enabled profiling.
- FeiLib: eight async/script regression groups; separate profiling enabled and
  compiled-out tests; HTTP lifecycle and worker database lifetime checks.
- Frontend: TypeScript/Vite build and Chrome administration tests, including
  group metadata, profiling display, upload, errors and rollback.
  Editor tests include pending loads, stale responses, retained associations,
  disabled controls during save and a rejected draft remaining editable.

Shutdown rejects new article saves, cancels pending hook calls, drains scripts
while model workers remain available, then closes workers and HTTP loops. Stop
hook duration and failures are included in profiling.

Database fixtures copy schema only into private mount/network namespaces.
No live article or comment database is written. Profiling microbenchmarks are
local optimized instrumentation measurements, not production throughput claims.

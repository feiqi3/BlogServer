# Built-in scripting

Build with `-DFEILIB_ENABLE_SCRIPT=ON` (default OFF). Scripting is compiled into
FeiLib. QuickJS and the runtime bridge are private; applications use
`Script/FScriptEngine.h` without QuickJS types, components or separate libraries.

## Server setup

```cpp
#include "Script/FScriptEngine.h"
int add(int a, int b) { return a + b; }

Fei::FScriptOptions options;
options.report = [](const Fei::FScriptExecution& event) { /* log status */ };
auto& scripts = server.scriptEngine(options); // before run()
FEI_SCRIPT_BIND(scripts, "add", add);
scripts.addScript("links", "/absolute/path/links.mjs", std::chrono::minutes(20));
server.run();
```

`bind` and `addScript` configure the engine before startup. `FHttpServer::run`
starts its job loop and initializes scripting there. Files are read through a
bounded FeiLib JobPool. Startup failures are reported without replacing native
controllers. `FHttpServer::stop` drains scripting before stopping its loops and
must run on a control thread. No script engine is created unless requested.

For standalone use, construct `FScriptEngine(loop, options)`, configure it,
call `start()`, then call `shutdown(onDrained)` and wait before stopping the loop.
Shutdown is asynchronous; do not block the loop waiting for itself. A shutdown
callback runs on the loop, except when an engine has never been started.

## Scripts and invocations

```js
export async function onStart() { /* initialize */ }
export async function onTick(input, call) {
  const result = await call.http.get('https://example.com/');
  if (result.status !== 200) throw Error('Link unavailable');
}
export async function onStop() { /* finish */ }
export async function check(input, call) {
  const result = await call.http.get(input.url);
  return {status: result.status, value: ctx.add(1, 2)};
}
```

```cpp
auto operation = scripts.invoke("links", "check",
    {{"url", "https://example.com/"}}, [](Fei::FScriptResult result) {
        if (result.ok()) { /* use result.value, a JSON value */ }
    });
// operation.cancel();
```

Completions run on the configured loop. Each invocation owns an ID, deadline,
result and cancellation state. `call.http.get` and `call.after` belong to that
invocation and are cancelled when it ends. Global `ctx.http` and `ctx.after`
are version-scoped conveniences; use `call` for automatic invocation cleanup.
Timers use callbacks: `await new Promise(resolve => call.after(100, resolve))`.

Requests share the runtime of their script version on its owning loop; they do
not create one VM per request. Normal invocation errors affect only that call.
Fatal runtime faults cancel the version's work. Periodic ticks start after
`onStart`, never overlap, and use a delay after successful completion. Failed
ticks pause scheduling. Independent invocations can coexist with a tick.
Generic calls are cancelled on shutdown; an active
background lifecycle hook may finish before `onStop` runs.

## Binding and reload

`bind` supports free functions using `int`, `double`, `bool`, `std::string` or
`ScriptValue` arguments, including const references. Results use JSON; `void`
returns null. Argument count, types and integer ranges are checked. Exceptions
become JavaScript errors. `bindFunction` accepts a JSON-array adapter for other
C++ callables. Bindings are configured before start and execute synchronously;
keep them short. `bindAsyncFunction("db.posts.get", adapter)` installs a native
Promise binding on `ctx` and invocation-scoped `call`. The adapter receives
script/hook/invocation identity plus JSON arguments, returns an operation handle,
and completes exactly once through its supplied callback. Marshal values as JSON;
never move QuickJS values across threads. Cancelled invocation results are ignored.

`options.profileInvocations` connects invocation timers to the generic
[Profiler](profiling.md) using IDs cached at load time. Start the profiler before
loading packages. `options.reportInvocations` separately enables string-based
execution observer events; it defaults to false and is not needed for profiling.

`loadFile` or `load` asynchronously prepares and starts a replacement. The
existing version keeps serving until validation and `onStart` succeed. Old
work drains after publication. The supplied interval replaces scheduling too;
zero disables automatic ticks. An optional short `beforePublish` callback lets
the host commit prepared metadata; a failure preserves the active version.

Module entry files use `.mjs` and exported hooks. Legacy `.js` entry files use
global hooks. Imports are relative and use an immutable source snapshot; no
filesystem access is exposed to JavaScript. Top-level asynchronous I/O is
unsupported. Host operations, including native bindings, are disabled during
validation; `onStart` may perform external effects that rollback cannot undo.

## Limits and scope

The engine uses FeiLib HTTP, loop and worker facilities. Script HTTP requests
verify TLS and reject non-public DNS results before opening connections unless
the host supplies another immutable address policy. Redirects are not followed.

Defaults include an 8 MiB JS heap per runtime, 100 ms execution slices, 64 KiB
invocation JSON, 8192-byte URLs, and bounded request/timer/invocation counts.
Sources are bounded to 32 files, 128 KiB per file and 512 KiB total. These are
resource controls, not an OS sandbox for hostile native code.

Server owns authorization, source persistence, administration and blog behavior.
BlogServer provides authenticated upload and rollback at `/background/scripts`;
see `Server/ScriptRuntime/README.md` for article hooks and configuration. Arbitrary
script HTTP routes remain pending. Enabling a local build does not deploy it.

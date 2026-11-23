# Script administration helpers

The runtime, HTTP/timer bindings, scheduling and live-version ownership are
built into FeiLib. See `FeiLib/docs/script-system.md` for the public API.

Server owns source persistence and administration policy. `ScriptService` is a
control-thread adapter around `FScriptEngine`; it contains no VM implementation.
`ScriptVersions` persists source bundles and commits their active/previous record
only after the candidate validates and starts successfully.

```cpp
Blog::Script::ScriptVersions versions(loop, directory,
    std::chrono::minutes(20), reporter);
versions.restore();
auto id = versions.upload(bundle);
if (!versions.active().previous.empty()) versions.rollback();
versions.shutdown(); // before stopping the loop
```

The SHA-256 store is server-owned, verifies source integrity and replaces its
active record atomically. It does not promise power-loss durability or rollback
of external side effects performed during script startup.

BlogServer can load a local script with scripting enabled:

```text
ScriptFile: /absolute/path/links.mjs
ScriptIntervalMs: 1200000
ScriptMemoryLimitMiB: 16
```

The configured script engine is owned and drained by `FHttpServer`. Existing
blog and comment databases are not used for script version storage.

`ScriptMemoryLimitMiB` defaults to `8` and accepts integers `1..1024`. Add it to
the active environment in `Server/resources/config/server.cfg`, then restart.
It caps each script version's QuickJS heap, including strings, objects and
ArrayBuffers; concurrent requests share that heap. It applies to file scripts,
admin uploads, rollback and hot-reload candidates. Invalid values disable script
startup and log an error; native routes still start. Old and candidate versions
have separate heaps during reload, so this is not a process-wide memory cap.
Native HTTP buffers, JSON limits and the 8 MiB binary in-flight budget remain
separate. The configured cap is an allocation limit, not preallocated memory.

## Admin upload

Build with `-DFEILIB_ENABLE_SCRIPT=ON`, build blogweb with `npm run build`, and
publish its pages/assets through the existing web deployment flow. Add these
settings to the selected environment in `Server/resources/config/server.cfg`:

```text
ScriptAdminEnabled: true
ScriptStoreDirectory: /absolute/path/script-versions
ScriptIntervalMs: 1200000
ScriptMemoryLimitMiB: 16
```

Log in to the existing admin page, then open `/background/scripts`. Select
`.js`/`.mjs` files or a module directory, select its root entry, and upload.
An `.mjs` entry exports hooks; a legacy `.js` entry defines global functions:

```js
// main.mjs
export function onStart() {}
export async function onTick() {
    const response = await ctx.http.get('https://example.com/');
    if (response.status !== 200) throw new Error('Request failed');
}
export function onStop() {}
```

The first admin version manages one package named `blog`, with relative module
imports. Uploads activate after validation and successful `onStart`. Failed
updates retain the running version; rollback starts the previous version.
Old work drains after publication. Startup external effects cannot be undone.
Ticks do not overlap; a failed tick pauses scheduling. Re-uploading identical
active source is idempotent. Upload a changed version to restart paused code.

`ScriptFile` is an optional bootstrap when no active version is stored. Restarts
restore the stored version even if that file no longer exists. Admin mode is
off by default. The interval defaults to 20 minutes; `0` disables ticks, and
positive values must be 10..86400000 ms. Without `ScriptStoreDirectory`, storage
defaults to `Server/resources/scripts/versions`. Keep it outside the public web
directory and include it in server backups.

Limits: 32 JS files, ASCII paths, root entry, 128 KiB per file, 512 KiB source
total, 4 MiB encoded JSON. The store holds at most 100 versions (including
failed starts); after that, remove inactive versions offline before uploading.
Do not remove the `current` or `previous` version. Execution events (64) and
management jobs (32) are memory-only and reset at restart.

The API uses existing admin cookies and no CORS grants. Writes additionally
require JSON, `X-Blog-Script-Request: 1`, and a matching Origin when provided:

```js
const response = await fetch('/api/scripts', {
    method: 'POST', credentials: 'same-origin',
    headers: {'Content-Type': 'application/json', 'X-Blog-Script-Request': '1'},
    body: JSON.stringify({entry: 'main.mjs', files: {'main.mjs': source}})
});
const {jobId} = await response.json(); // HTTP 202; poll GET /api/scripts/jobs/{id}
```

`GET /api/scripts` returns cached version status, jobs and execution events.
`POST /api/scripts/rollback` accepts `{}`. Jobs finish as `succeeded` or `failed`;
HTTP 202 means accepted, not activated. HTTP 429 means another operation is
running; 401 requires login; 403 rejects an unsafe write; 501 means admin mode
is unavailable. Status reads perform no disk I/O. File work uses FeiLib JobPool,
and runtime work uses the server's FeiLib job loop. Stop administration before
stopping that loop. Arbitrary inbound script routes are pending.

## Article hooks

Export named hook groups from the active package, then select a group in the
article editor. Groups share the package runtime; visits do not create a VM.

```js
export const article = {
    async OnSubmit(event, call) {
        const category = await call.db.categories.get(event.draft.category_id);
        if (!category) throw new Error('Category missing');
        return {title: event.draft.title.trim()};
    },
    async OnRequest(event, call) {
        return {profile: event.post.profile + ' — welcome'};
    }
};
```

Root `OnSubmit`/`OnRequest` exports form the `default` group. Hook names are case
sensitive. `OnSubmit` receives `{draft, operation, postId}`, returns a field patch
or null, and finishes before persistence. Errors/timeouts reject the save.
Concurrent edits detected during preprocessing require reload/retry. `OnRequest`
receives `{post, postId, request: {path, method}}`; its patch affects only this
render. Script/patch errors and the 2-second invocation timeout use the original
article. Capacity/render failures return errors. No configured association keeps
the existing native path, with no extra SQL, JS call or task submission.

Async model operations: `call.db.posts.get(id)`, `.list({limit,offset})`,
`.update(id, patch)`, and `call.db.categories.get(id)`/`.list(options)`. IDs are
decimal strings. Lists accept limits 1..100 and offsets 0..100000. Post lists
return summaries. Model writes do not trigger article hooks and are forbidden
inside `OnSubmit`; return the processed draft instead. `ctx.db` provides the
same reads for lifecycle hooks; use invocation-scoped `call` APIs for requests.
The Server bounds invocation input/output JSON at 256 KiB.
During shutdown, pending article calls are cancelled and new submissions receive
HTTP 503. `onStop` may still perform model reads before workers and loops close.

[Friend avatar example](../../docs/friend-avatars.md) combines binary HTTP,
`friends.OnSubmit` and periodic refresh with a persistent local fallback cache.

## Profiling

```text
ProfilingEnabled: true
ProfilingDirectory: /data/blog/profiling
```

Restart to apply. Defaults to disabled; enabling collects native scopes and
script invocation durations, including awaited work. Admin statistics and file
locations are described in [Server profiling](../../docs/profiling.md).

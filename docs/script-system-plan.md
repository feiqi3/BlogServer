# Script system implementation plan

Script support is an optional BlogServer build feature, disabled by default and not deployed. Preserve existing
FeiLib behavior and use its event loop, HTTP client and routing facilities.

| Phase | Status | Scope | Acceptance |
| --- | --- | --- | --- |
| QuickJS runtime | Prototype tested | Pin engine, memory limit, execution budget, thread ownership | Windows and Linux isolated tests cover exceptions, limits and Promise jobs |
| FeiLib outbound HTTP | Prototype tested | Bind `ctx.http.get` to `FHttpClient` and its loop | Local HTTP success, failure, Promise resume and cancellation pass |
| Timers and native bindings | Prototype tested | One-shot FeiLib timer and explicit free-function registration | Timer, shutdown cancellation, argument validation and native exceptions pass |
| FeiLib integration | Local milestone passed | Built into the existing FeiLib target; private QuickJS and bindings | Public C++/JSON API and complete Server startup/drain pass |
| Built-in FScriptEngine | Local milestone passed | Existing FeiLib target; private QuickJS; no public components or sessions | Public shared-library consumer, 20 concurrent calls and Server-owned startup/drain pass |
| Background execution | Local-file milestone passed | Optional Server-owned service, async lifecycle, recurring trusted-local-script jobs, structured results and shutdown | Full Server build and isolated startup/API/stop pass; accelerated intervals verify repeated execution, no overlap, restart reload and failure paths |
| Module loading | Baseline tests passed | `.mjs` exports, relative dependency loading and per-runtime source snapshots | Bounded imports cannot escape the snapshot; old/new runtimes retain their own dependency versions |
| Resource and access controls | Baseline tests passed | Script quotas, post-DNS connection policy, unhandled rejection tracking and failure cleanup | Loop abuse, memory limits, private/mapped addresses, hostname resolution and cancelled callbacks are tested |
| Upload and hot reload | Local milestone passed | Authenticated admin page, async management jobs, persisted versions, automatic activation and rollback | Real Server API and Chrome interaction checks pass; invalid updates retain old code; duplicates, rollback and process restart restore pass |
| Script HTTP routes | Not started | Register nonconflicting paths and extend FeiLib asynchronous responses cautiously | HTTP/1.1 and HTTP/2 requests complete once, handle disconnect and timeout, and existing controllers remain unchanged |
| Existing-route script hooks | Deferred | Attach scripts to existing backend page handling after the core system is stable | Existing page behavior and hook ordering remain testable; async hooks use FeiLib request infrastructure |
| Deployment | Not started | Remote build, isolated validation, backup and rollback | Production health checks pass; existing blog and comment databases are not overwritten |

## Product decisions

- Integrate scripting directly into FeiLib through one public FScriptEngine.
- HTTP, timers and native registration are private bindings, not public components.
- Server retains authorization, persistence policy, administration and blog logic.
- The public API is specified in FeiLib/docs/script-system.md. Components and public QuickJS/session types have been removed.

- Successful uploads activate automatically; failed validation retains the active version.
- Execution results are visible in the admin interface; notifications are deferred.
- Scripts may register any unoccupied route. Conflicts fail activation without
  replacing an existing controller or another script.
- The first version exposes limited capabilities explicitly, not arbitrary
  filesystem, database or shell access.
- First admin release manages one package with multiple JS modules. Upload and
  rollback return HTTP 202 with a job ID; file I/O stays outside HTTP/JS loops.
- History is bounded to 100 versions; management jobs and execution events are
  memory-only. Retention UI and multiple named packages remain future work.

## Immediate next work

1. Design FeiLib asynchronous HTTP continuations before modifying its response lifecycle.
2. Add script routes, conflict detection, per-request cancellation and deadlines.
3. Validate HTTP/1.1 and HTTP/2, disconnects and native-controller coexistence.
4. Add existing-page hooks after the route foundation is stable.

## Latest validation

- Eight Linux test groups pass, including public engine concurrency, version activation and existing FeiLib async regressions.
- Windows runtime, native function binding, module snapshot and rejection tests pass.
- Full Server build and namespace-isolated module/lifecycle/API/shutdown smoke pass with scripting built into FeiLib.
- A consumer compiled against public headers and the actual FeiLib shared library passes.
- Disabled scripting excludes runtime sources and compiles FHttpServer plus the changed host/controller paths without engine dependencies; this is not a full disabled Server runtime test.
- Periodic ticks continue while independent asynchronous invocations wait;
  lifecycle overlap checks exclude ordinary requests. A regression reproduces
  the previous unintended pause and passes after the script-layer fix.
- Real admin API tests cover authentication, origin/header guards, limits, modules,
  async startup, 20 concurrent blog requests, failed uploads, rollback and persisted
  restart restore without the bootstrap file. Fixtures use schema-only temporary databases.
- Chrome tests cover file selection, task polling, failed-update state, rollback,
  unavailable/login states, escaped output and mobile navigation/layout.

## Background milestone validation

- Full Linux Server build with `FEILIB_ENABLE_SCRIPT=ON` passed.
- `Script.FeilibHttpBinding` and `Script.BackgroundLifecycle` passed.
- Lifecycle tests cover asynchronous hooks, non-overlapping ticks, fresh loading
  across two process runs, rejected startup and paused execution after tick failure.
- An isolated full Server used a temporary schema-only database, certificates,
  configuration, mount namespace and network namespace. Recurring HTTP requests,
  the blog categories API, `onStop` and graceful process exit passed.
- Interval tests use accelerated 20/50 ms settings; a 20-minute interval is
  configured as `ScriptIntervalMs: 1200000`, not timed for 20 minutes in tests.
- No production deployment or production database replacement was performed.

The embedding is not an OS security boundary. FScriptEngine rejects non-public
DNS results before connecting unless the embedder supplies another immutable
policy. Admin uploads require explicit local configuration. No production
deployment or production database replacement was performed.

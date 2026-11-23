# Friend avatar cache

The JS job in [main.mjs](../Server/Scripts/FriendLinks/main.mjs) downloads avatars
through FeiLib binary HTTP. Server bindings persist bounded images; visitors use
the original avatar, then Gravatar mirrors when applicable, then the local cache.
A missing cache ends with the existing placeholder. Ordinary comments keep their
existing mirror order and final default image.

Build with scripting enabled and deploy matching Server and blogweb builds.
Add settings to the active environment in `Server/resources/config/server.cfg`:

```text
ScriptAdminEnabled: true
ScriptIntervalMs: 1200000
ScriptMemoryLimitMiB: 8
AvatarCacheDirectory: /data/blog/cache/avatars
```

Upload `main.mjs` in `/background/scripts`, then select `friends` in the friend
article's hook selector and save. The example finds the article titled `友链`;
change `ARTICLE_TITLE` to match a custom `LinksPageTitle`. Keep existing packages:
add this file as `friend-avatars.mjs`, and re-export `friends` from the current
entry. Call its `onStart`, `onTick` and `onStop` from existing lifecycle hooks
rather than replacing unrelated hooks. For example:

```js
import * as avatars from './friend-avatars.mjs';
export const friends = avatars.friends;
export async function onStart() { await avatars.onStart(); }
export async function onTick() { await avatars.onTick(); }
export function onStop() { avatars.onStop(); }
```

Existing Markdown stays unchanged:

````markdown
```links
[Example](https://example.org) | https://example.org/avatar.png | Description
```
````

`friends.OnSubmit` queues version-scoped work and returns immediately. It does
not delay persistence or change the draft. Startup and periodic ticks scan the
stored article; they recover failed submissions/restarts without visitor work.
Successful avatars refresh after 24 hours. Downloads run two at a time, at most
64 unique URLs per scan, with a 5-second timeout per hop and at most 3 redirects.
HTTPS downgrades, private network destinations and compressed responses fail.
Failures retain the last successful file and retry at a later tick.

`ctx.avatarCache.get(url)` returns metadata or null;
`ctx.avatarCache.store(url, base64)` validates and atomically replaces one record.
Invocation-scoped `call.avatarCache` exposes the same APIs. The example converts
ArrayBuffers to base64 only for this existing JSON native-binding boundary.
It does not require changes to FeiLib bindings or arbitrary filesystem access.
`db.posts.findByTitle(title)` is a read-only model lookup.

Each image is limited to 128 KiB; accepted formats are PNG, JPEG, GIF, WebP and
SVG. Larger images need a smaller source URL; there is no automatic resizing.
Raster metadata is inspected, without decoding every pixel. SVG records reject
scripts/DOCTYPE/entities/foreignObject and use a sandbox CSP with external
resources disabled. URLs are limited to 2048 UTF-8 bytes; the cache holds at most
256 source URLs. It does not evict good avatars automatically.

Records use SHA-256 filenames under `AvatarCacheDirectory`, defaulting to
`Server/resources/cache/avatars`. No cache bytes or secrets belong in Git.
Include this directory in server backups; keep it outside public static folders.
The public, read-only `/avatar-cache?source=<encoded-original-url>` endpoint
never downloads anything. It supports ETag validation and a 10-minute browser
cache. A missing record returns 404 with `no-store`. Native file work uses a
bounded FeiLib JobPool and callbacks return to the existing Server job loop.

The script can refresh an avatar before a submitted article finishes saving;
cache files are independent of article transactions. Disabling or rolling back
the script leaves successful cache records available. Stop script work before
draining the cache worker and Server loop.

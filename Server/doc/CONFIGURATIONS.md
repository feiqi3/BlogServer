# Configureations available (BlogServer / Server layer)

FeiLib 层配置（Logger/TCP/SSL/Http/Http2）见 `FeiLib/README.md`。
本文档只记录 **Blog 业务层**读取的配置项，均写在 `Server/resources/config/server.cfg` 的 `[[prod]]`（或 `[[test]]`）段下。

格式：`Key: value`（冒号分隔，一行一项）。全部可省略，省略时用默认值。

## Auth

AdminUser --- Admin account name for backyard login.
default `"admin"`

AdminPassword --- Admin account password for backyard login.
default `"admin"`

## Database

Database --- Path to the sqlite database file.
Relative to resource dir. default `"resources/database/blog.db"`

## Cache

SessionHoldTime --- Session expire time, in milliseconds.
default `3600000` (1 hour)

TemplateCacheTime --- Inja template cache hold time, in milliseconds.
Templates are cached after first render; change this to control how soon template edits take effect. default `86400000` (24 hours). NOTE: server restart always refreshes templates.

FileCacheHoldTime --- Static file cache hold time, in milliseconds.
default `3600000` (1 hour)

## Blog Pages

AboutPageTitle --- Post title used by `/about` route.
The route queries Posts by this title and redirects to `/post/{id}`.
default `"关于网站"`

LinksPageTitle --- Post title used by `/links` route. (2026-08-22)
The route queries Posts by this title and redirects to `/post/{id}`.
default `"友链"`

### Friend Links Page Setup

The friend links page is a regular blog post with `hide=1` and `allow_rss=0`, so it does not appear in the homepage, category listing, archive, or RSS feed, but is accessible via direct link `/post/{id}` or the `/links` shortcut.

**Content format** — the post body uses a fenced `links` code block with pipe-delimited fields:

```
```links
[名称](URL) | 图片URL | 描述
[飞起的博客](https://feiqi3.cn) | https://pic.feiqi3.cn/avatar.jpg | 游戏开发与图形学
[GitHub](https://github.com) | https://github.githubassets.com/favicons/favicon.svg | Where the world builds software
```
```

Each line inside the `links` block:
1. `[name](url)` — markdown link syntax, the friend site name and URL
2. `| image URL` — avatar/logo image (optional, omit to show text-only card)
3. `| description` — short description shown below the name (optional)

The frontend (`article-inline.ts`) renders these blocks as a card grid with avatars. Lines that do not match the format are skipped.

## Media

defaultPic --- Default picture path used when creating a category without a picture.
No default (unset means feature disabled).

## Qiniu CDN

QiNiu_Ak --- Qiniu access key, for SSL cert auto update to CDN. (dormant, not wired to any route)
QiNiu_Sk --- Qiniu secret key, same as above.

## Analytics

GoatCounterUrl --- Base URL for the self-hosted GoatCounter analytics service.
When set, article detail pages (`/post/{id}`) embed the GoatCounter tracking script pointing to `{GoatCounterUrl}/count`.
When unset (or empty), the tracking script is not embedded (feature disabled).
default: empty (disabled)

Dev example: `GoatCounterUrl: http://localhost:23367`
Prod example: `GoatCounterUrl: https://stats.feiqi3.cn`

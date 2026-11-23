// Upload this module, or re-export its hooks from the active package's entry.
const ARTICLE_TITLE = '友链'; // Match LinksPageTitle when customized.
const REFRESH_MS = 24 * 60 * 60 * 1000;
const MAX_BYTES = 128 * 1024;
const TIMEOUT_MS = 5000;
const MAX_URLS = 64;
const CONCURRENCY = 2;
const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
let stopping = false;
let nextUrl = 0;
const queued = new Set();
const known = new Map();
const inFlight = new Set();
function remember(url, record) {
    known.delete(url); known.set(url, record);
    if (known.size > 256) known.delete(known.keys().next().value);
}

function urls(content) {
    const found = new Set();
    let inLinks = false, fence = '';
    for (const line of String(content).split(/\r?\n/)) {
        const trimmed = line.trim();
        if (!inLinks) {
            const match = trimmed.match(/^(`{3,}|~{3,})links\s*$/);
            if (match) { inLinks = true; fence = match[1]; }
            continue;
        }
        if (trimmed.startsWith(fence)) { inLinks = false; continue; }
        const match = trimmed.match(/^\[[^\]]+\]\([^)]+\)\s*\|\s*([^|]*)/);
        const url = match?.[1].trim();
        if (url && /^https?:\/\/[^\s]+$/.test(url) && url.length <= 2048) found.add(url);
    }
    return found;
}

function base64(buffer) {
    const bytes = new Uint8Array(buffer), parts = [];
    // Small chunks avoid a large argument list or one JS object per four bytes.
    for (let begin = 0; begin < bytes.length; begin += 768) {
        let part = '';
        for (let i = begin; i < Math.min(begin + 768, bytes.length); i += 3) {
            const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2];
            part += alphabet[a >> 2] + alphabet[((a & 3) << 4) | ((b ?? 0) >> 4)] +
                (i + 1 < bytes.length ? alphabet[((b & 15) << 2) | ((c ?? 0) >> 6)] : '=') +
                (i + 2 < bytes.length ? alphabet[c & 63] : '=');
        }
        parts.push(part);
    }
    return parts.join('');
}

function redirect(base, location) {
    // QuickJS has no browser URL object. Support ordinary absolute/root/query/
    // relative HTTP locations; the native client validates each resolved URL.
    if (/^https?:\/\//.test(location)) return location;
    const match = base.match(/^(https?):\/\/([^/?#]+)([^?#]*)(?:\?[^#]*)?/);
    if (!match || /[\s\\]/.test(location)) throw Error('Invalid avatar redirect');
    const origin = `${match[1]}://${match[2]}`;
    if (location.startsWith('//')) return `${match[1]}:${location}`;
    if (location.startsWith('/')) return origin + location;
    if (location.startsWith('?')) return origin + (match[3] || '/') + location;
    if (/^[a-z][a-z0-9+.-]*:/i.test(location)) throw Error('Unsupported avatar redirect');
    const path = (match[3] || '/').replace(/[^/]*$/, '') + location;
    const normalized = [];
    for (const part of path.split('/')) {
        if (part === '..') normalized.pop(); else if (part !== '.') normalized.push(part);
    }
    return origin + normalized.join('/');
}

async function download(source) {
    let url = source;
    for (let hop = 0; hop <= 3; ++hop) {
        const response = await ctx.http.get(url, {responseType: 'arrayBuffer', maxBytes: MAX_BYTES, timeoutMs: TIMEOUT_MS});
        if ([301, 302, 303, 307, 308].includes(response.status)) {
            const location = response.headers.find(([name]) => name.toLowerCase() === 'location')?.[1];
            if (!location || hop === 3) throw Error('Avatar redirect limit');
            const next = redirect(url, location);
            if (url.startsWith('https:') && !next.startsWith('https:')) throw Error('Avatar TLS downgrade');
            url = next; continue;
        }
        if (response.status !== 200) throw Error('Avatar status ' + response.status);
        const encoding = response.headers.find(([name]) => name.toLowerCase() === 'content-encoding')?.[1];
        if (encoding && encoding.toLowerCase() !== 'identity') throw Error('Encoded avatar unsupported');
        if (!response.body.byteLength) throw Error('Empty avatar');
        return response.body;
    }
    throw Error('Avatar redirect limit');
}

async function refresh(url) {
    let record = known.get(url);
    if (!record) record = await ctx.avatarCache.get(url);
    if (record && Date.now() - record.cachedAt < REFRESH_MS) { remember(url, record); return; }
    const bytes = await download(url);
    if (!stopping) remember(url, await ctx.avatarCache.store(url, base64(bytes)));
}

function pump() {
    while (!stopping && inFlight.size < CONCURRENCY && queued.size) {
        const url = queued.values().next().value;
        queued.delete(url); inFlight.add(url);
        // Intentionally version-scoped: submission completion must not cancel
        // the refresh or make article persistence depend on a remote avatar.
        refresh(url).catch(() => { /* Keep the previous successful cache. */ }).finally(() => {
            inFlight.delete(url); pump();
        });
    }
}
function enqueue(content) {
    if (stopping) return;
    const sources = Array.from(urls(content));
    if (!sources.length) { nextUrl = 0; return; }
    nextUrl %= sources.length;
    const now = Date.now();
    for (let scanned = 0; scanned < Math.min(MAX_URLS, sources.length) && queued.size < MAX_URLS; ++scanned) {
        const url = sources[nextUrl];
        nextUrl = (nextUrl + 1) % sources.length;
        const cached = known.get(url);
        if (!inFlight.has(url) && (!cached || now - cached.cachedAt >= REFRESH_MS)) queued.add(url);
    }
    pump();
}
async function scan() {
    if (stopping) return;
    const post = await ctx.db.posts.findByTitle(ARTICLE_TITLE);
    if (post) enqueue(post.content);
}

export async function onStart() { await scan(); }
export async function onTick() { await scan(); }
export function onStop() { stopping = true; queued.clear(); }
export const friends = {
    OnSubmit(event) { enqueue(event.draft.content); return null; }
};

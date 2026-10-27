/**
 * Cache API layer under WebResourceFetcher's runtime downloads.
 *
 * Core preloads (shaders, fonts, LUTs, textures_low) ship inside SolarSystem.data and never
 * come through here. This is for what C++ streams afterwards (DDS/KTX2 textures, planet
 * manifests): a second visit satisfies those from the Cache API instead of the network.
 *
 * C++ reaches it through window['__solarSystemFetchAsset'] (WebResourceFetcher.cpp), a
 * bracket-notation hook like the Module['…'] callbacks: Release links with --closure 1,
 * which renames dotted property reads. The hook is a plain function and this file is
 * ordinary TypeScript, so it is unit-tested without a WASM build.
 */

/** Resolves the body, or a number: the HTTP status of a failure (0 = network / truncated). */
export type AssetFetchResult = Uint8Array<ArrayBuffer> | number;
export type AssetFetcher = (url: string) => Promise<AssetFetchResult>;

export interface AssetFetcherOptions {
    /** Runtime asset base (VITE_ASSET_BASE or the deployed base). Versions the cache name. */
    assetBase: string;
    /** Page URL that relative asset URLs resolve against. */
    baseHref: string;
    /** `undefined` where the Cache API does not exist (insecure origin, some embedded views). */
    caches: Pick<CacheStorage, 'open' | 'keys' | 'delete'> | undefined;
    fetch: typeof fetch;
    log?: (message: string) => void;
}

const CACHE_PREFIX = 'solarsystem-assets:';

/**
 * One cache per asset base: a deploy that moves textures to a new base URL (a versioned CDN
 * path) starts clean instead of pinning the old ones, and the old cache is deleted.
 */
export function assetCacheName(assetBase: string, baseHref: string): string {
    let base = assetBase;
    try {
        base = new URL(assetBase, baseHref).href;
    } catch {
        // Keep the raw string: a name only has to be stable.
    }
    return `${CACHE_PREFIX}${base.endsWith('/') ? base : `${base}/`}`;
}

/**
 * Whether a response is a texture/manifest worth keeping. Only a complete, successful body
 * is stored: a partial or failed one would be served again on every later visit. Vite's SPA
 * fallback answers a missing file with index.html and a 200, so the content type is checked
 * too.
 */
export function isCacheableAsset(response: Pick<Response, 'ok' | 'status' | 'headers'>, bodyBytes: number): boolean {
    if (!response.ok || response.status !== 200) return false;
    if ((response.headers.get('content-type') ?? '').toLowerCase().startsWith('text/html')) return false;
    // Content-Length counts encoded bytes; only comparable when nothing was decoded on the way.
    const length = Number(response.headers.get('content-length'));
    const decoded = (response.headers.get('content-encoding') ?? 'identity').toLowerCase() !== 'identity';
    if (!decoded && response.headers.has('content-length') && Number.isFinite(length) && length !== bodyBytes) {
        return false;
    }
    return true;
}

export function createAssetFetcher(options: AssetFetcherOptions): AssetFetcher {
    const log = options.log ?? (() => {});
    const name = assetCacheName(options.assetBase, options.baseHref);
    const cacheStorage = options.caches;
    let cachePromise: Promise<Cache | null> | null = null;

    const openCache = (): Promise<Cache | null> => {
        if (!cacheStorage) return Promise.resolve(null);
        cachePromise ??= (async () => {
            try {
                const cache = await cacheStorage.open(name);
                // Drop caches left by other asset bases (an older deploy) in the background.
                void cacheStorage.keys().then((keys) => Promise.all(
                    keys.filter((key) => key.startsWith(CACHE_PREFIX) && key !== name)
                        .map((key) => cacheStorage.delete(key)),
                )).catch(() => {});
                return cache;
            } catch (error) {
                log(`[AssetCache] unavailable (${String(error)}); fetching without it`);
                return null;
            }
        })();
        return cachePromise;
    };

    return async (url: string): Promise<AssetFetchResult> => {
        const key = new URL(url, options.baseHref).href;
        const cache = await openCache();

        if (cache) {
            try {
                const hit = await cache.match(key);
                if (hit) {
                    const body = new Uint8Array(await hit.arrayBuffer());
                    log(`[AssetCache] hit ${key} (${body.byteLength} bytes)`);
                    return body;
                }
            } catch (error) {
                log(`[AssetCache] read failed for ${key}: ${String(error)}`);
            }
        }

        let response: Response;
        let body: Uint8Array<ArrayBuffer>;
        try {
            response = await options.fetch(key);
            if (!response.ok) return response.status;
            body = new Uint8Array(await response.arrayBuffer());
        } catch {
            return 0;
        }

        if (cache && isCacheableAsset(response, body.byteLength)) {
            try {
                // A fresh Response over the bytes we already hold: the stored entry is exactly
                // what is handed to MEMFS, and nothing is left to be cut short mid-write.
                await cache.put(key, new Response(body, {
                    status: 200,
                    headers: { 'content-type': response.headers.get('content-type') ?? 'application/octet-stream' },
                }));
            } catch (error) {
                log(`[AssetCache] could not store ${key}: ${String(error)}`); // quota, private mode
            }
        }
        return body;
    };
}

/** `?assetcache=0|1` overrides; the dev server defaults to off so edited textures are not pinned. */
export function assetCacheEnabled(search: string, isDev: boolean): boolean {
    const value = new URLSearchParams(search).get('assetcache');
    if (value === '0' || value === 'off' || value === 'false') return false;
    if (value === '1' || value === 'on' || value === 'true') return true;
    return !isDev;
}

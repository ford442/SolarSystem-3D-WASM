import { describe, expect, it, vi } from 'vitest';
import { assetCacheEnabled, assetCacheName, createAssetFetcher, isCacheableAsset } from './assetCache.js';

class FakeCacheStorage {
    readonly stores = new Map<string, Map<string, Response>>();
    open = vi.fn(async (name: string) => {
        if (!this.stores.has(name)) this.stores.set(name, new Map());
        const store = this.stores.get(name)!;
        return {
            match: async (key: string) => store.get(key)?.clone(),
            put: async (key: string, response: Response) => { store.set(key, response); },
        } as unknown as Cache;
    });
    keys = vi.fn(async () => [...this.stores.keys()]);
    delete = vi.fn(async (name: string) => this.stores.delete(name));
}

const BASE = 'https://example.test/app/';
const bytes = (...values: number[]) => new Uint8Array(values);

function setup(fetchImpl: typeof fetch, caches: FakeCacheStorage | undefined = new FakeCacheStorage(), assetBase = BASE) {
    const fetchSpy = vi.fn(fetchImpl);
    const fetcher = createAssetFetcher({
        assetBase, baseHref: BASE, caches: caches as unknown as CacheStorage | undefined, fetch: fetchSpy as typeof fetch,
    });
    return { fetcher, fetchSpy, caches };
}

const ok = (body: Uint8Array<ArrayBuffer>, headers: Record<string, string> = {}) =>
    async () => new Response(body, { status: 200, headers });

describe('createAssetFetcher', () => {
    it('stores a good body on a miss and serves the second request without the network', async () => {
        const { fetcher, fetchSpy } = setup(ok(bytes(1, 2, 3), { 'content-type': 'image/vnd-ms.dds' }));
        const url = `${BASE}resource/textures/Earth_Diffuse.dds`;

        expect(await fetcher(url)).toEqual(bytes(1, 2, 3));
        expect(await fetcher(url)).toEqual(bytes(1, 2, 3));
        expect(fetchSpy).toHaveBeenCalledTimes(1);
    });

    it('resolves relative URLs against the page so the key is stable', async () => {
        const { fetcher, fetchSpy } = setup(ok(bytes(9)));
        await fetcher('resource/a.dds');
        await fetcher(`${BASE}resource/a.dds`);
        expect(fetchSpy).toHaveBeenCalledTimes(1);
    });

    it('reports an HTTP failure as its status and stores nothing', async () => {
        const { fetcher, fetchSpy, caches } = setup(async () => new Response('nope', { status: 404 }));
        expect(await fetcher(`${BASE}missing.dds`)).toBe(404);
        expect(await fetcher(`${BASE}missing.dds`)).toBe(404);
        expect(fetchSpy).toHaveBeenCalledTimes(2);
        expect([...caches!.stores.values()].every((store) => store.size === 0)).toBe(true);
    });

    it('reports a network error or a body that dies mid-read as status 0 and stores nothing', async () => {
        const { fetcher, caches } = setup(async () => { throw new TypeError('Failed to fetch'); });
        expect(await fetcher(`${BASE}a.dds`)).toBe(0);

        const broken = setup(async () => {
            const stream = new ReadableStream({ pull(controller) { controller.error(new Error('reset')); } });
            return new Response(stream, { status: 200 });
        });
        expect(await broken.fetcher(`${BASE}b.dds`)).toBe(0);
        expect([...caches!.stores.values()].every((store) => store.size === 0)).toBe(true);
        expect([...broken.caches!.stores.values()].every((store) => store.size === 0)).toBe(true);
    });

    it('does not keep the SPA fallback page a missing file is answered with', async () => {
        const { fetcher, fetchSpy } = setup(ok(bytes(60, 33), { 'content-type': 'text/html; charset=utf-8' }));
        await fetcher(`${BASE}resource/textures/Gone.dds`);
        await fetcher(`${BASE}resource/textures/Gone.dds`);
        expect(fetchSpy).toHaveBeenCalledTimes(2);
    });

    it('does not keep a body shorter than its Content-Length', async () => {
        const { fetcher, fetchSpy } = setup(ok(bytes(1, 2), { 'content-length': '1000' }));
        await fetcher(`${BASE}cut.dds`);
        await fetcher(`${BASE}cut.dds`);
        expect(fetchSpy).toHaveBeenCalledTimes(2);
    });

    it('does not keep a 206 partial response', async () => {
        const { fetcher, fetchSpy } = setup(async () => new Response(bytes(1), { status: 206 }));
        await fetcher(`${BASE}part.dds`);
        await fetcher(`${BASE}part.dds`);
        expect(fetchSpy).toHaveBeenCalledTimes(2);
    });

    it('versions the cache by asset base and deletes the other bases\' caches', async () => {
        const caches = new FakeCacheStorage();
        caches.stores.set(assetCacheName('https://old.test/v1/', BASE), new Map());
        caches.stores.set('unrelated', new Map());
        const { fetcher } = setup(ok(bytes(1)), caches);
        await fetcher(`${BASE}a.dds`);
        await new Promise((resolve) => setTimeout(resolve, 0));

        expect([...caches.stores.keys()].sort()).toEqual([assetCacheName(BASE, BASE), 'unrelated'].sort());
        expect(assetCacheName('https://old.test/v1', BASE)).toBe(assetCacheName('https://old.test/v1/', BASE));
        expect(assetCacheName('https://old.test/v1/', BASE)).not.toBe(assetCacheName('https://new.test/v2/', BASE));
    });

    it('still downloads when the Cache API is missing or throws', async () => {
        const none = setup(ok(bytes(5)), undefined);
        expect(await none.fetcher(`${BASE}a.dds`)).toEqual(bytes(5));

        const broken = new FakeCacheStorage();
        broken.open.mockRejectedValue(new Error('SecurityError'));
        const { fetcher } = setup(ok(bytes(6)), broken);
        expect(await fetcher(`${BASE}a.dds`)).toEqual(bytes(6));
    });
});

describe('isCacheableAsset', () => {
    const response = (status: number, headers: Record<string, string>) => new Response(null, { status, headers });
    it('rejects a short identity-encoded body', () => {
        expect(isCacheableAsset(response(200, { 'content-length': '4' }), 3)).toBe(false);
    });
    it('accepts a complete identity-encoded body and ignores Content-Length when it was decoded', () => {
        expect(isCacheableAsset(response(200, { 'content-length': '3' }), 3)).toBe(true);
        expect(isCacheableAsset(response(200, { 'content-length': '3', 'content-encoding': 'gzip' }), 30)).toBe(true);
        expect(isCacheableAsset(response(200, {}), 3)).toBe(true);
    });
});

describe('assetCacheEnabled', () => {
    it('is on in production, off in dev, and the query string wins', () => {
        expect(assetCacheEnabled('', false)).toBe(true);
        expect(assetCacheEnabled('', true)).toBe(false);
        expect(assetCacheEnabled('?assetcache=0', false)).toBe(false);
        expect(assetCacheEnabled('?assetcache=1', true)).toBe(true);
    });
});

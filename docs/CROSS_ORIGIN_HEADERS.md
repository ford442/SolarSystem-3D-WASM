# Cross-origin headers (COEP, COOP, CORP, CORS)

How HTTP response headers interact with the WASM host page, a separate asset CDN, and `emscripten_async_wget2` / `emscripten_wget_data` fetches.

**Related:** [README § Runtime asset hosting](../README.md#runtime-asset-hosting), [docs/ARCHITECTURE.md §6](ARCHITECTURE.md#6-runtime-asset-hosting).

---

## 1. Does this app need COEP?

**No — not with the current build.** The Emscripten link flags use `ASYNCIFY` only; there is no `-s PTHREAD` / `-s SHARED_MEMORY`. The runtime does not require `SharedArrayBuffer` or `crossOriginIsolated`.

| Build flag | Present? | Needs COEP+COOP? |
|------------|----------|------------------|
| `ASYNCIFY=1` | Yes | No |
| `PTHREAD` / `SHARED_MEMORY` | No | Yes (for `SharedArrayBuffer`) |

**Recommendation:** Prefer **Option A** (no COEP) for production unless you add pthreads later. COEP simplifies nothing today and forces every cross-origin asset to opt in via CORP.

If you add pthreads later, switch to **Option B**.

---

## 2. Header matrix

### Option A — Recommended today (no cross-origin isolation)

| Response | Origin | COEP | COOP | CORP | `Access-Control-Allow-Origin` |
|----------|--------|------|------|------|-------------------------------|
| App HTML, JS, WASM | `app.example.com` | omit | omit | omit | omit (same-origin) |
| DDS, MP3, manifest | same origin as app | omit | omit | omit | omit |
| DDS, MP3, manifest | `cdn.example.com` (subdomain CDN) | omit | omit | `cross-origin` | `*` or `https://app.example.com` |

Cross-origin XHR (`emscripten_async_wget2`) needs **CORS only** (`Access-Control-Allow-Origin`). CORP is optional without COEP but harmless as `cross-origin`.

### Option B — Cross-origin isolated (future pthread / SharedArrayBuffer)

| Response | Origin | COEP | COOP | CORP | `Access-Control-Allow-Origin` |
|----------|--------|------|------|------|-------------------------------|
| App HTML, JS, WASM | `app.example.com` | `require-corp` | `same-origin` | `same-origin` | omit |
| DDS, MP3, manifest | `cdn.example.com` | **omit** | **omit** | `cross-origin` | `*` or `https://app.example.com` |
| DDS, MP3, manifest | same origin as app | omit | omit | omit | omit |

**Critical rules under Option B:**

1. **COEP/COOP belong on the app document**, not on static assets.
2. Cross-origin assets consumed while COEP is active must expose **`Cross-Origin-Resource-Policy: cross-origin`** (or be same-origin).
3. `Access-Control-Allow-Origin` alone satisfies CORS for XHR; CORP is the COEP companion header.
4. Do **not** put `Cross-Origin-Embedder-Policy: require-corp` on CDN DDS responses — it is a document policy, not an asset policy, and confuses operators (see §4).

### Same-origin assets (both options)

When `VITE_ASSET_BASE` is unset, dev/preview/production load `resource/...` from the **same origin** as the Vite app (`/solar-system/resource/...`). No CORS or CORP headers are required.

---

## 3. How Emscripten fetches assets

`WebResourceFetcher` resolves paths against `window.__solarSystemAssetBase` (from `VITE_ASSET_BASE` or the page base URL), then:

| API | Used for | Transport |
|-----|----------|-----------|
| `emscripten_async_wget2` | Core load, staged manifests, optional audio | `XMLHttpRequest` GET → MEMFS |
| `emscripten_wget_data` | LOD sync fetch | blocking XHR-style download |

Both use **CORS-enabled XHR** for cross-origin URLs. Failed fetches log to the console (`Failed to download … Status: N`) and invoke callbacks with `success=false` — they do not fail silently if DevTools is open.

**Subdomain CDN example:**

```text
App:   https://app.example.com/solar-system/
Assets: VITE_ASSET_BASE=https://cdn.example.com/solar-system/2026.07.0/
Fetch: https://cdn.example.com/solar-system/2026.07.0/resource/textures_low/Earth_Day_Diffuse_Low.dds
```

---

## 4. test.1ink.us audit (2026-07-14)

The live host applies a **global Apache header block** to HTML **and** DDS:

```http
cross-origin-embedder-policy: require-corp
cross-origin-opener-policy: same-origin
access-control-allow-origin: *
cross-origin-resource-policy: cross-origin
```

| Finding | Impact |
|---------|--------|
| COEP on DDS responses | **Misconfiguration** — COEP is for documents, not textures. Harmless for XHR but misleading. Remove from asset vhost. |
| CORP + ACAO on DDS | **Correct** for cross-origin fetches under COEP |
| App + assets same host | CORS/CORP are redundant but present |
| `crossOriginIsolated === true` | Achieved, but unused without pthreads |

Fetches work because CORP and CORS are set. The problematic pattern is **COEP on the CDN**, not CORP itself.

---

## 5. Production recommendations

### App origin (`app.example.com`)

**Preferred (Option A):**

```apache
# Apache — app vhost only
Header always set Cross-Origin-Resource-Policy "same-origin"
# No COEP/COOP
```

**If adding pthreads later (Option B):**

```apache
Header always set Cross-Origin-Embedder-Policy "require-corp"
Header always set Cross-Origin-Opener-Policy "same-origin"
Header always set Cross-Origin-Resource-Policy "same-origin"
```

Also ensure `.wasm` is served as `Content-Type: application/wasm`.

### Asset CDN (`cdn.example.com`)

```apache
# Apache / CDN response headers — static assets only
Header always set Access-Control-Allow-Origin "*"
Header always set Access-Control-Allow-Methods "GET, HEAD, OPTIONS"
Header always set Cross-Origin-Resource-Policy "cross-origin"
# Do NOT set Cross-Origin-Embedder-Policy or Cross-Origin-Opener-Policy here
```

For credentialed fetches (not used by this app), replace `*` with the app origin:

```http
Access-Control-Allow-Origin: https://app.example.com
Vary: Origin
```

### Nginx equivalents

```nginx
# App server
add_header Cross-Origin-Embedder-Policy "require-corp" always;  # Option B only
add_header Cross-Origin-Opener-Policy "same-origin" always;       # Option B only

# CDN / asset location
add_header Access-Control-Allow-Origin "*" always;
add_header Cross-Origin-Resource-Policy "cross-origin" always;
```

### S3 / CloudFront

- **Bucket CORS:** allow `GET`, `HEAD` from app origin (or `*`).
- **Response headers policy (CDN):** inject `Cross-Origin-Resource-Policy: cross-origin` and `Access-Control-Allow-Origin`.
- **Do not** attach COEP to the asset distribution.

---

## 6. Local testing

### Vite dev / preview (app isolation headers)

`web/vite.config.ts` sets COEP+COOP on the **app server** so you can reproduce Option B locally:

```bash
./build-web.sh
cd web
npm run preview   # http://127.0.0.1:4173/solar-system/ — COEP+COOP on HTML/JS
```

### Mock asset CDN (cross-origin)

Terminal 1 — app:

```bash
cd web && npm run preview
```

Terminal 2 — mock CDN on a different port (CORS + CORP, no COEP):

```bash
cd web && npm run serve:mock-cdn
# http://127.0.0.1:5199/
```

Terminal 3 — build/run with cross-origin assets:

```bash
cd web
VITE_ASSET_BASE=http://127.0.0.1:5199/ npm run build
npm run preview
# or for dev hot-reload:
VITE_ASSET_BASE=http://127.0.0.1:5199/ npm run dev
```

### Automated verification

```bash
cd web
npm run serve:mock-cdn &          # port 5199
npm run preview &                 # port 4173 (restart after vite.config change)
npm run verify:cross-origin       # XHR same-origin + cross-origin under COEP
npm run verify:textures           # full WASM load + skybox path
```

**Pass criteria:** no `requestfailed` events, XHR status 200, non-zero byte length, console shows `Successfully downloaded:` from C++ for runtime fetches.

### Network tab checklist

| Request | Expected status | Headers to verify (cross-origin CDN) |
|---------|-----------------|--------------------------------------|
| `index.html` | 200 | COEP+COOP (if Option B) |
| `SolarSystem.wasm` | 200 | same as HTML |
| `resource/.../*.dds` | 200 | `access-control-allow-origin`, `cross-origin-resource-policy: cross-origin` |
| Failed DDS | 404 with CORS | Error responses must also include ACAO + CORP |

---

## 7. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| XHR status 0, COEP active | Missing CORP on CDN | Add `Cross-Origin-Resource-Policy: cross-origin` |
| CORS error in console | Missing ACAO | Configure bucket/CDN CORS |
| `Failed to download … Status: 404` | Asset not deployed | Upload path; check `VITE_ASSET_BASE` trailing slash |
| COEP on CDN only, app without COEP | N/A — unusual | Remove COEP from CDN vhost |
| `crossOriginIsolated` false in dev | Vite headers missing | Use updated `vite.config.ts`; restart dev server |

---

## 8. Quick reference

```text
Same-origin assets     → no CORS/CORP/COEP needed
Subdomain CDN          → ACAO + CORP:cross-origin on assets; COEP only on app (if pthreads)
test.1ink.us pattern   → works but COEP on DDS should be removed from CDN config
This build (ASYNCIFY)  → COEP optional; omit for simpler ops
```

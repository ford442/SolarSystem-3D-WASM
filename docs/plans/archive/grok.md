# grok.md

## Project Overview

**SolarSystem-3D-WASM** is a 3D solar system simulator built in C++17 and compiled to WebAssembly using Emscripten. It renders an animated, high-fidelity Solar System using WebGL 2 (OpenGL ES 3.0) with advanced graphics (atmospheric scattering, PCF/ray-traced shadows, normal mapping, lens flare, HDR).

Key web-specific features for performance:
- **Staged planet loading** (per-system manifests): Planets (and moons/rings/clouds) are not initialized or downloaded until the camera approaches their orbital activation radius (800–1500 units). Proxy 3D markers show name + "(approach to load)" or "[downloading XX%]".
- **LOD texture streaming**: All planets start with low-res textures (`resource/textures_low/*_Low.dds`, 4×4 placeholders in repo). High-res upgrades are queued on-demand when camera < 50 units. 
- **Hysteresis + downgrade**: High-res stays loaded until distance > 100 units (2× threshold); far planets downgrade back to low-res to free VRAM.
- **Quality presets** (`g_qualityPreset`): 0=low (force downgrade, no upgrades), 1=medium, 2=full. Set via `?quality=` URL or JS `Module.SetQualityPreset(0|1|2)`.
- **Async resilience**: `WebResourceFetcher::DownloadFile` (emscripten_async_wget2) for staged init + `TextureLoadingQueue` (dedup, backoff retries, CancelLoad/deprioritize, progress stats) for high-res streaming.
- **Safe mipmaps**: `nv_dds` + `TextureImage2D` always declare `GL_TEXTURE_BASE_LEVEL=0` + `MAX_LEVEL=<last uploaded>` (critical for WebGL 2 incomplete texture -> black). Fallbacks are explicit 4×4 grey checker with MAX=0.
- Streaming UI overlay for high-res progress; initial loading bar for core + staged.

Native desktop build remains full high-res immediate load with no LOD/staged.

## Tech Stack

- **Language**: C++17 (single source for native + WASM)
- **Web Graphics**: WebGL 2 / OpenGL ES 3.0 via Emscripten
- **Native Graphics**: OpenGL 4.6 Core + GLEW
- **Build**: CMake + `emcmake`/`emmake` (web); CMake + Ninja (native)
- **Textures**: DDS (DXT1/3/5 + uncompressed) via custom `nv_dds` loader
- **Key Web Subsystems**:
  - `PlanetSystemManifest` + `_planetSystemManifests` (staged: NOT_LOADED → DOWNLOADING → READY)
  - `Planet::LoadHighResIfClose` / `UnloadHighResIfFar` (per-planet overrides, queue-driven)
  - `WebResourceFetcher` (async + sync Fetch)
  - `TextureLoadingQueue` (serialized high-res, cancel, stats)
  - `glBindTextureUnit` polyfill + `Shader::Set*Double` float cast (WebGL limits)
  - Vite + TypeScript frontend (progress hooks, camera pose teleport for testing)

## Build & Run (Web Target)

From project root (Emscripten on PATH or use `--no-emsdk` if already sourced):

```bash
./build-web.sh
```

Artifacts:
- `build-web/SolarSystem.{js,wasm,data}` → copied to `web/src/` + `web/public/`

### Dev (hot-reload TS; use remote assets for real LOD in dev)
```bash
cd web
npm install
npm run dev
# Open http://localhost:5173/solar-system/
```

### Preview (uses bundled placeholder low-res textures)
```bash
cd web
npm run build   # runs build:emcc + tsc + vite
npm run preview
# Open http://localhost:4173/solar-system/
```

### Raw Emscripten output
```bash
cd build-web
python3 -m http.server 8000
# Open http://localhost:8000/SolarSystem.html
```

**Critical**: Never use `file://`. Always HTTP. Real high-res textures + skybox live on the deployment origin (dev mode forces `https://test.1ink.us/solar-system/`).

Native (Windows only):
```bash
./build.sh
cd build
./SolarSystem
```

## Key Files

| Path | Purpose |
|------|---------|
| `src/Application.cpp` | RunOneFrame, UpdatePlanetSystemLoading, UpdateLOD, RenderPlanetProxyMarkers, RenderTextureLoadingProgress, manifests, quality preset handling |
| `src/Application.h` | `PlanetSystemManifest` (assetPaths + optionalAssetPaths, activationRadius, pending/total, state), `g_qualityPreset` extern |
| `src/Solar_System/Planet.h` + `*/Earth.cpp` etc. | `_isHighResLoaded`/`_isHighResLoading`/`_lastCameraDistance`/`_lodThreshold`; impl of Load/Reload + downgrade logic per planet |
| `src/Auxiliary_Modules/TextureLoadingQueue.{h,cpp}` | Queue, dedup via `_pendingPaths`, `_cancelledPaths`, backoff `nextAttemptTime`, ProcessQueue, CancelLoad, stats |
| `src/Auxiliary_Modules/WebResourceFetcher.{h,cpp}` | `DownloadFile` (async_wget2 for staged), `Fetch` (sync for LOD) |
| `src/3rdparty/nv_dds.cpp` + `TextureImage2D.{h,cpp}` | `lastUploadedLevel` tracking + unconditional `BASE_LEVEL`/`MAX_LEVEL`; ReloadTexture + post-generate fix |
| `web/src/main.ts` + `index.html` | `updateLoadingProgress`, `updateStreamingProgress`, `__solarSystemAssetBase`, streaming DOM bar |
| `resource/textures_low/` | All low-res + 4×4 placeholders for moons/rings/clouds |

## Testing LOD + Staged Locally (from docs alone)

1. `./build-web.sh && cd web && npm run preview`
2. Open browser at the preview URL (note `/solar-system/` base).
3. Watch console + streaming overlay. Initially only core assets + "Sun + labels + camera work". Planets appear as 3D proxy markers (labels like "Earth (approach to load)").
4. Use exposed JS to teleport: `window.setCameraPose(x, y, z, yaw, pitch)`. Sun at (0,0,0). E.g. approach Earth proxy (~1900,0,0).
5. Fly/zoom inside activation radius → staged download starts (console "[StagedLoading]", proxy % updates in 3D label, then planet appears with low-res).
6. Zoom camera to < ~50 units of a planet center → `[LOD] ... Queueing high-res...` ; streaming bar shows "High-res upgrade: N/M (%)" ; console success + texture reload. (With placeholders visual delta is minimal, but logs/UI prove path.)
7. Fly far (>100 units) → downgrade log + reload low-res.
8. Test preset: append `?quality=0` (forces low) or call `Module.SetQualityPreset(0)`.
9. Network tab: confirm only low + staged required at first; high-res on close. Errors (404) → graceful fallback (keeps low).
10. No black textures: mip safety + fallbacks ensure it.

See:
- `docs/plans/TESTING_GUIDE.md` (expanded for full flow)
- `docs/plans/VERIFICATION_CHECKLIST.md`
- `docs/plans/staged_loading_plan.md`
- `docs/plans/LOD_IMPLEMENTATION.md`

## Known Gotchas & Safety

- Black textures almost always = missing/incorrect `GL_TEXTURE_MAX_LEVEL` after DDS upload or glGenerateMipmap. Code sets it explicitly everywhere.
- High-res never preloaded in WASM manifests; only low + models for staged systems.
- Optional moon/ring/cloud textures are fire-and-forget (may 404 locally if not deployed; fallback used).
- Quality=0 + far distance both force downgrade path.
- `AdjustToParent(float timeScale)` (scaled sim) and camera transitions use `glm::mix` ~2s.
- `requestAnimationFrame` may show FPS=0 when tab unfocused — not a freeze.
- Only web target runs here; native links Windows libs.

## Workflow & Guidelines

1. Edit C++ (use `#ifdef __EMSCRIPTEN__` for web differences).
2. `./build-web.sh`
3. `cd web && npm run dev` (or preview) + hard refresh.
4. Test via console logs, streaming bar, 3D proxy labels, `setCameraPose`, Network tab.
5. For LOD changes: always verify downgrade path + preset low + queue cancel + MAX_LEVEL on reload.
6. Document polyfills/workarounds; keep manifest optionals non-blocking.

## Current Architecture Notes (as of 2026-06)

- Staged + LOD are **layered**: staged controls when planet+low-res appears; LOD (post-READY) does high-res upgrade/downgrade.
- All 9 systems (Mercury→Pluto + moons/rings) covered via manifests + per-planet LoadHighRes overrides.
- Progress: initial loading bar (core+staged required) + separate throttled streaming overlay (high-res queue).
- TextureImage2D::ReloadTexture used for both upgrade and downgrade (rebinds every frame in render passes).

For full contributor testing of LOD/staged/async without real assets: the 4×4 placeholders + logs + UI + teleport make the paths verifiable from docs + console alone.

Related: GitHub issues #51–#58 (see docs/plans/* for cross-links).

(End of cleaned grok.md — no paste artifacts, accurate build, current LOD/staged details.)
# AGENTS.md — AI agent notes

**Canonical project docs:** [README.md](README.md) (build, deploy, assets) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (system design, loading, LOD).

This file only records environment-specific facts for automated agents (e.g. Cursor Cloud). Do not duplicate architecture here.

---

## Runnable targets (Cursor Cloud / Linux VM)

- **Web (WASM):** primary target for day-to-day work — `./build-web.sh`, then `cd web && npm run dev`.
- **Native Linux:** `./build.sh` now builds with system packages (see README apt list). Run from repo root: `./build/SolarSystem`. Full textures under `resource/textures/` are deploy artifacts; without them init may fail on skybox load.

## Emscripten toolchain

- SDK path: `/content/build_space/emsdk` (v6.0.0). `build-web.sh` and `setup_web_dependencies.sh` auto-source it.
- Prebuilt deps persist in the snapshot: `external/glm/`, `external/assimp/build-wasm/lib/libassimp.a`.
- Rebuild WASM after C++ changes: `./build-web.sh` (copies artifacts to `web/src/` and `web/public/`).
- Skip emsdk sourcing if `emcc` is already on PATH: `./build-web.sh --no-emsdk`.

## Build / lint / run (web)

```bash
./build-web.sh                              # C++ → WASM Release (slow)
./build-web.sh --debug                      # Debug symbols (-O0 -g)
cd web && npx tsc                           # TypeScript check (closest to lint)
cd web && npm run dev                       # http://localhost:5173/solar-system/
cd web && npm run preview                   # http://localhost:4173/solar-system/
```

- Base path is `/solar-system/` — opening `/` alone 404s.
- TypeScript hot-reloads in dev; C++ requires `./build-web.sh` + browser refresh.
- Wipe `build-web/` when switching Debug↔Release or changing LTO/exception flags.
- Emscripten flag model / Wasm exceptions fallback: [docs/plans/PORTING_GUIDE.md](docs/plans/PORTING_GUIDE.md) §3b.
- No automated test suite; verification is manual (see [docs/plans/TESTING_GUIDE.md](docs/plans/TESTING_GUIDE.md)).

## Runtime assets in this environment

- Git contains **4×4 placeholder** DDS files only. Full planet textures and the 6K skybox are deploy artifacts under `resource/textures/` (see [docs/ARCHITECTURE.md §6](docs/ARCHITECTURE.md#6-runtime-asset-hosting)).
- **Dev, preview, and production all default to same-origin assets** (`VITE_ASSET_BASE` unset → page base URL). Bundled placeholders live under `web/public/resource/textures_low/` after `./build-web.sh`.
- To test against a remote CDN: `VITE_ASSET_BASE=https://your-cdn.example/solar-system/VERSION/ npm run dev`
- COEP/CORS/CORP rules: [docs/CROSS_ORIGIN_HEADERS.md](docs/CROSS_ORIGIN_HEADERS.md). Vite dev/preview sets COEP+COOP on the app; use `npm run serve:mock-cdn` + `npm run verify:cross-origin` for subdomain CDN simulation.
- Without high-res assets deployed, expect: procedural **Sun**, 3D labels, camera navigation — planet surfaces and skybox may show placeholders or stay minimal.
- **FPS counter may read 0** when the tab is unfocused (`requestAnimationFrame` throttling). Move the mouse or press WASD to confirm the loop is alive.

## Quick test helpers

```js
// Browser console — teleport camera (Sun at origin)
window.setCameraPose(x, y, z, yaw, pitch);  // yaw=0 → +X, yaw=90 → +Z
Module.SetQualityPreset(0);                  // force low-res, no LOD upgrades
```

Planets load when the camera enters each manifest's activation radius (800–1500 units). Use teleport to trigger staged loading without flying across orbital distances.

## Agent constraints (do not violate)

1. Use `#ifdef __EMSCRIPTEN__` for web-specific code; never invert as the primary branch.
2. Never add a blocking `while` main loop on WASM — use `RunOneFrame()`.
3. Never spawn `std::thread` in the web build.
4. Never use geometry/compute shaders on web.
5. Never call `glUniform1d` on web — use `Shader::Set*Double`.
6. Never open HTML via `file://`.
7. Do not commit credentials; `web/deploy.py` reads from environment variables.

*Last updated: 2026-07-14*

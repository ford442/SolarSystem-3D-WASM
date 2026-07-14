# SolarSystem-3D-WASM — Architecture

Canonical architecture reference for contributors. For build commands and deployment, start with [README.md](../README.md). For hands-on verification, see [docs/plans/TESTING_GUIDE.md](plans/TESTING_GUIDE.md). For Emscripten porting details, see [docs/plans/PORTING_GUIDE.md](plans/PORTING_GUIDE.md).

---

## 1. Overview

**SolarSystem-3D-WASM** is a C++17 3D Solar System renderer with two targets from one codebase:

| Target | Graphics | Windowing | Audio |
|--------|----------|-----------|-------|
| **Native desktop** | OpenGL 4.6 Core + GLEW | GLFW3 | irrKlang |
| **Web (WASM)** | WebGL 2 / OpenGL ES 3.0 | GLFW3 (Emscripten port) | SDL_mixer |

The web build adds async resource loading, staged planet initialization, and distance-based texture LOD. The native build loads everything synchronously at startup with full-resolution textures only.

**Graphics features:** atmospheric scattering, PCF/ray-traced shadows, cloud shadows, lens flare, HDR, normal mapping, planetary rings.

---

## 2. Repository layout

```
SolarSystem-3D-WASM/
├── src/                        # C++ application
│   ├── Application.h/.cpp      # Main loop, loading, rendering
│   ├── Auxiliary_Modules/      # Engine subsystems (Shader, Camera, LOD queue, …)
│   └── Solar_System/           # Celestial bodies by system (Earth_System/, …)
├── resource/                   # Runtime assets
│   ├── shaders/                # GLSL (preloaded into WASM VFS)
│   ├── textures/               # High-res DDS (hosted at deploy time, not all in git)
│   ├── textures_low/           # Low-res DDS + 4×4 placeholders (committed)
│   ├── models/, fonts/, icons/, sounds/
│   └── asset-manifest.json     # Inventory + optional SHA-256 checksums
├── web/                        # Vite + TypeScript frontend
│   ├── src/main.ts             # WASM bootstrap, progress UI, asset base URL
│   └── public/                 # SolarSystem.wasm, .data, bundled placeholders
├── build-web.sh                # Emscripten build + artifact deploy
└── docs/
    ├── ARCHITECTURE.md         # This file
    └── plans/                  # Testing guide, porting guide, future plans
```

---

## 3. Application lifecycle

### 3.1 Entry and main loop

Native and web share `Application::RunOneFrame()`. The web build **must not** use a blocking `while` loop — it registers the frame callback via `emscripten_set_main_loop_arg`.

```cpp
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg([](void* arg) {
        static_cast<Application*>(arg)->RunOneFrame();
    }, this, 0, 1);
#else
    while (!glfwWindowShouldClose(_mainWindow)) {
        RunOneFrame();
    }
#endif
```

`RunOneFrame()` handles two application states on WASM:

| State | Behavior |
|-------|----------|
| `LOADING` | Black screen, progress bar updates, waits for `_resourcesPending == 0`, then `InitSceneObjects()` |
| `RUNNING` | Input, simulation, rendering, staged planet loading, LOD updates |

### 3.2 Startup flow (WASM)

```
Application()
    → InitSystems()          # GLFW, OpenGL, audio
    → InitScene()
        → LoadCoreResources()    # async download of core assets
        → LoadOptionalSounds()   # fire-and-forget music tracks
        → _appState = LOADING
    → Exec() → RunOneFrame() loop

When _resourcesPending <= 0:
    → InitSceneObjects()     # Sun, skybox, shaders, manifests (no planets yet)
    → _appState = RUNNING
```

Desktop skips async loading: `InitScene()` calls `InitSceneObjects()` immediately and sets `RUNNING`.

---

## 4. Resource loading (three layers)

Web asset loading is layered. Each layer serves a different purpose.

### 4.1 Layer 1 — Core load (`LoadCoreResources`)

Downloads a **small fixed set** before the scene can run (~15 files: models, sun textures, skybox faces). Does **not** include planet textures.

```cpp
void Application::InitScene() {
#ifdef __EMSCRIPTEN__
    LoadCoreResources();
    LoadOptionalSounds();
#else
    InitSceneObjects();
    _appState = AppState::RUNNING;
#endif
}
```

Progress is tracked with `_totalResources` / `_resourcesPending` and reported to the frontend via `window.updateLoadingProgress(loaded, total)`.

**Skybox special case:** Core load requests **high-res** skybox URLs (`resource/textures/Main_SkyBox/*.dds`) but writes them into **low-res MEMFS paths** (`resource/textures_low/Main_SkyBox/*.dds`). If the fetch fails (offline dev, missing CDN), the bundled 4×4 placeholder faces from the `.data` preload remain usable.

| Tier | Path | In git? | Typical use |
|------|------|---------|-------------|
| High-res skybox | `resource/textures/Main_SkyBox/{PositiveX,NegativeX,…}.dds` | No (deploy artifact) | Fetched at core load |
| Low-res placeholder | `resource/textures_low/Main_SkyBox/{PositiveX,NegativeX,…}.dds` | Yes (4×4) | Fallback + MEMFS target |

### 4.2 Layer 2 — Staged planet loading

Planets are **not** created at startup on WASM. Each system has a `PlanetSystemManifest` (loaded from `resource/planet_manifest.json` on WASM):

```cpp
struct PlanetSystemManifest {
    std::string name;
    glm::vec3 proxyPosition;
    float activationRadius;              // ~800 inner, ~1500 outer planets
    std::vector<std::string> assetPaths;           // required (block init)
    std::vector<std::string> optionalAssetPaths;   // moons/rings; 404 tolerated
    std::function<void()> initFunc;
    enum class State { NOT_LOADED, DOWNLOADING, READY };
    int pendingDownloads, totalDownloads;
};
```

Every frame, `UpdatePlanetSystemLoading()` checks camera distance:

```
NOT_LOADED + dist < activationRadius
    → DOWNLOADING (async WebResourceFetcher::DownloadFile per asset)
    → pendingDownloads == 0 → initFunc() (e.g. InitEarthSystem()) → READY
```

While `NOT_LOADED` or `DOWNLOADING`, `RenderPlanetProxyMarkers()` draws 3D labels such as `Earth (approach to load)` or `Earth [downloading 42%]`.

Desktop: all manifests are empty; `InitStarSystem()` creates every planet immediately.

**Manifest file (`resource/planet_manifest.json`):** Preloaded into the WASM bundle alongside shaders. At startup, `WebResourceFetcher::Fetch` may overwrite it from the CDN (hotfix without WASM rebuild). Bump the `version` field when publishing an updated manifest so operators can track/cache-bust deployments. Each entry uses `proxyPosition` as an offset from the Sun, `required` / `optional` asset paths, and an `init` tag bound to `InitXxxSystem()` in C++ (`Mercury`, `EarthSystem`, …).

```json
{
  "version": 1,
  "systems": [{
    "name": "Earth",
    "init": "EarthSystem",
    "proxyPosition": [1900, 0, 0],
    "activationRadius": 800,
    "required": ["resource/textures_low/Earth_Day_Diffuse_Low.dds"],
    "optional": ["resource/textures_low/Moon_Diffuse_Low.dds"]
  }]
}
```

### 4.3 Layer 3 — LOD texture streaming

After a planet is `READY`, it renders with **low-res** textures (`GetTexturePath` returns `textures_low/` on WASM). Each frame, `UpdateLOD()` calls `planet->LoadHighResIfClose(cameraPos)`:

| Condition | Action |
|-----------|--------|
| Distance < 50 units (`_lodThreshold`) | Queue high-res textures via `TextureLoadingQueue` |
| Distance > 100 units (2× threshold) | Downgrade back to low-res |
| `g_qualityPreset == 0` | Force low-res, no upgrades |

High-res loads are serialized (one in-flight), deduplicated, cancellable on rapid camera movement, and reported through `window.updateStreamingProgress(completed, total, active)`.

**Mipmap safety:** `nv_dds` + `TextureImage2D` always set `GL_TEXTURE_BASE_LEVEL=0` and `GL_TEXTURE_MAX_LEVEL` to the last uploaded mip level. Incomplete mip chains cause black textures on WebGL 2.

---

## 5. System flow diagrams

### 5.1 WASM startup and loading

```
┌─────────────────────────────────────────────────────────────┐
│              Application::InitScene() [WASM]                 │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
                   LoadCoreResources()
                   (models, sun, skybox)
                   _appState = LOADING
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  RunOneFrame() while LOADING                                 │
│    UpdateLoadingProgress() → JS progress bar                 │
│    if _resourcesPending <= 0:                                │
│      InitSceneObjects() → _appState = RUNNING                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  RunOneFrame() while RUNNING                                 │
│    UpdatePlanetSystemLoading()   # staged manifests          │
│    UpdateLOD()                   # high-res queue            │
│    RenderPass()                                              │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 C++ ↔ JavaScript progress bridge

```
Application (_resourcesPending, _totalResources)
    → UpdateLoadingProgress()
    → EM_ASM → window.updateLoadingProgress(loaded, total)
    → #progress-bar, #loading-container (initial load)

RenderTextureLoadingProgress()
    → EM_ASM → window.updateStreamingProgress(completed, total, active)
    → #streaming-progress (high-res LOD overlay)
```

### 5.3 LOD upgrade path

```
Planet READY with low-res texture
    → LoadHighResIfClose(cameraPos)
    → distance < 50u → TextureLoadingQueue::Enqueue(highResPath)
    → TextureImage2D::ReloadTexture(highResPath)
    → render passes rebind GetTexture() each frame (hot-swap safe)
```

---

## 6. Runtime asset hosting

Large DDS textures and MP3 music are **release artifacts**, not fully committed to git. The repo ships 4×4 placeholder DDS files under `resource/textures_low/` so local builds never 404 on path resolution.

### 6.1 How URLs are resolved

`web/src/main.ts` sets the runtime asset origin:

```typescript
const deployedBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
const runtimeAssetBase = import.meta.env.VITE_ASSET_BASE?.trim() || deployedBaseUrl.toString();
window.__solarSystemAssetBase = runtimeAssetBase;  // consumed by WebResourceFetcher
```

`WebResourceFetcher` resolves every `resource/...` request as:

```
{VITE_ASSET_BASE or page base URL} + resource/...
```

### 6.1 Dev, preview, and production (single rule)

| Mode | Command | Default asset origin | Notes |
|------|---------|---------------------|-------|
| **Dev** | `npm run dev` | Same-origin (`http://localhost:5173/solar-system/`) | TS hot-reload; uses bundled placeholders unless `VITE_ASSET_BASE` is set |
| **Preview** | `npm run preview` | Same-origin (`http://localhost:4173/solar-system/`) | Serves `dist/`; placeholders from `web/public/resource/` |
| **Production** | deployed `dist/` | Same-origin under `/solar-system/` | Or separate CDN via build-time `VITE_ASSET_BASE` |

**Optional remote assets:** Point at any host that serves a `resource/` tree:

```bash
VITE_ASSET_BASE=https://assets.example.com/solar-system/2026.07.0/ npm run dev
VITE_ASSET_BASE=https://assets.example.com/solar-system/2026.07.0/ npm run build
```

The value is the directory **containing** `resource/`, with a trailing slash. It is public configuration — never put credentials in it.

### 6.2 What is preloaded vs fetched

| Asset class | WASM `.data` preload | Runtime fetch |
|-------------|---------------------|---------------|
| Shaders, fonts, icons | Yes | — |
| Low-res placeholders | Partial (via build copy to `web/public/`) | Core + staged + LOD |
| High-res textures | No | LOD queue + skybox core load |
| Sounds | No (may be absent in checkout) | Optional parallel download |

See [README.md § Runtime asset hosting](../README.md#runtime-asset-hosting) for CDN layout, manifest checksums, and `web/deploy.py` targets. For COEP/CORS/CORP header matrix and subdomain CDN setup, see [docs/CROSS_ORIGIN_HEADERS.md](CROSS_ORIGIN_HEADERS.md).

---

## 7. C++ module map

### 7.1 Core

| Module | Role |
|--------|------|
| `Application` | Scene graph, render passes, loading orchestration, quality presets |
| `SystemModules.h` | Platform-conditional OpenGL/audio includes; `glBindTextureUnit` polyfill on WASM |
| `Shader` | GLSL compile/link; `Set*Double` casts to float on WASM |

### 7.2 Auxiliary

| Module | Role |
|--------|------|
| `Camera`, `FPS_Handler` | First-person navigation |
| `ShadowMapFBO` | PCF / ray-traced shadows |
| `HDR`, `LensFlare` | Post-processing |
| `TextureImage2D` | DDS load, reload, mip level management |
| `WebResourceFetcher` | `DownloadFile` (async) + `Fetch` (sync) — WASM only |
| `TextureLoadingQueue` | Serialized high-res LOD downloads |

### 7.3 Scene objects

Inheritance: `SpaceObject` → `Transformable` → `Planet` / `Satellite` / `Star`.

Each planet system lives in `src/Solar_System/<Name>_System/`. `SolarSystem.h` aggregates includes. Atmospheres, clouds, and rings are separate render components.

---

## 8. Web frontend (`web/`)

| File | Role |
|------|------|
| `index.html` | Canvas, loading overlay, settings panel, streaming progress bar |
| `src/main.ts` | Module init, `updateLoadingProgress`, `updateStreamingProgress`, `setCameraPose`, settings persistence |
| `vite.config.ts` | Base path `/solar-system/` |
| `public/SolarSystem.{wasm,data}` | Generated by `./build-web.sh` |

Rebuild WASM after C++ changes: `./build-web.sh` then refresh the browser.

---

## 9. Platform differences (quick reference)

### 9.1 Shaders

| Aspect | Native | Web |
|--------|--------|-----|
| Version | `#version 460 core` | `#version 300 es` |
| Precision | N/A | `precision highp float;` in fragments |
| Geometry/compute shaders | Allowed | **Not supported** |
| Double uniforms | `glUniform1d` | Cast to float via `Shader` class |

### 9.2 Threading

| Task | Native | Web |
|------|--------|-----|
| Background music | `std::thread` | `UpdateBackgroundMusic()` per frame |
| Nearest-planet search | `std::thread` | `UpdateSearchNearestPlanet()` every 60 frames |

### 9.3 Texture path helper

```cpp
std::string GetTexturePath(const std::string& lowRes, const std::string& highRes) {
#ifdef __EMSCRIPTEN__
    return lowRes;
#else
    return highRes;
#endif
}
```

---

## 10. Adding a new planet

1. Create `src/Solar_System/<Planet>_System/<Planet>.h/.cpp` inheriting `Planet` (and `Satellite` subclasses for moons).
2. Add includes to `SolarSystem.h`.
3. Declare `Init<Planet>System()` in `Application.h`; implement in `Application.cpp` using `GetTexturePath("resource/textures_low/...", "resource/textures/...")`.
4. **Desktop:** call from `InitStarSystem()` directly.
5. **WASM:** add a `PlanetSystemManifest` entry with `assetPaths`, `optionalAssetPaths`, `proxyPosition`, `activationRadius`, and `initFunc`.
6. **LOD (optional):** override `LoadHighResIfClose()` / `UnloadHighResIfFar()` on the planet class.

---

## 11. Common pitfalls

1. Do not use a blocking main loop on WASM.
2. Do not spawn `std::thread` in the web build.
3. Do not use geometry or compute shaders on web.
4. Route double uniforms through `Shader::Set*Double`, not `glUniform1d`.
5. Do not open the app via `file://` — always use HTTP.
6. Do not preload sounds in the Emscripten CMake block (Git LFS files may be missing).
7. After `TextureImage2D::ReloadTexture`, render passes must rebind texture IDs each frame.
8. Black textures on web usually mean incorrect `GL_TEXTURE_MAX_LEVEL` — verify mip chain upload.

---

## 12. Local verification (summary)

```bash
./build-web.sh
cd web && npm run preview
# Open http://localhost:4173/solar-system/
```

In the browser console:

```js
window.setCameraPose(1850, 20, 30, 90, -10);  // approach Earth proxy
```

Expect proxy labels → staged download → low-res planet → LOD streaming overlay when close. With placeholders only, visual quality change is minimal; console logs and UI prove the code paths.

Full scenarios: [docs/plans/TESTING_GUIDE.md](plans/TESTING_GUIDE.md).

---

## 13. Related documentation

| Document | Purpose |
|----------|---------|
| [README.md](../README.md) | Build, features, asset hosting, deployment |
| [docs/plans/TESTING_GUIDE.md](plans/TESTING_GUIDE.md) | Step-by-step LOD/staged testing |
| [docs/plans/PORTING_GUIDE.md](plans/PORTING_GUIDE.md) | Emscripten port history and CMake flags |
| [docs/plans/WEBGPU_COMPANION_PLAN.md](plans/WEBGPU_COMPANION_PLAN.md) | Future Three.js/WebGPU companion (opt-in) |
| [docs/CROSS_ORIGIN_HEADERS.md](CROSS_ORIGIN_HEADERS.md) | COEP/CORS/CORP matrix, CDN vs app headers, local verification |
| [AGENTS.md](../AGENTS.md) | AI agent environment notes (Cursor Cloud VM) |
| [docs/plans/archive/](plans/archive/) | Completed implementation plans and PR notes |

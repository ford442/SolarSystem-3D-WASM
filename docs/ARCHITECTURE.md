# SolarSystem-3D-WASM — Architecture

Canonical architecture reference for contributors. For build commands and deployment, start with [README.md](../README.md). For hands-on verification, see [docs/plans/TESTING_GUIDE.md](plans/TESTING_GUIDE.md). For Emscripten porting details, see [docs/plans/PORTING_GUIDE.md](plans/PORTING_GUIDE.md).

---

## 1. Overview

**SolarSystem-3D-WASM** is a C++17 3D Solar System renderer with two targets from one codebase:

| Target | Graphics | Windowing | Audio |
|--------|----------|-----------|-------|
| **Native desktop** | OpenGL 4.6 Core + GLEW | GLFW3 | irrKlang |
| **Web (WASM)** | WebGL 2 / OpenGL ES 3.0 | GLFW3 (Emscripten port) | HTMLAudioElement (`web/src/music.ts`) |

The web build adds async resource loading, staged planet initialization, and distance-based texture LOD. The native build loads everything synchronously at startup with full-resolution textures only.

**Graphics features:** atmospheric scattering, PCF/ray-traced shadows, cloud shadows, lens flare, HDR, normal mapping, planetary rings, instanced main-belt asteroid field + comet tails.

---

## 2. Repository layout

```
SolarSystem-3D-WASM/
├── src/                        # C++ application
│   ├── Application.h/.cpp      # Main loop, loading, simulation (+ PlatformWindow, InputHandler, …)
│   ├── Renderer.h, Scene*Renderer.cpp  # Draw passes over a FrameContext
│   ├── core/                   # Log, Camera, FrameTimer
│   ├── gfx/                    # GL objects: GlHandle, Shader/ShaderLibrary, Mesh, textures, FBOs
│   ├── render/                 # Overlays and post effects built on gfx/ (lens flare, text, paths, field lines)
│   ├── assets/                 # Downloads, texture streaming/LOD, manifests, texture formats
│   ├── astro/                  # GL-free ephemerides, sky events, observer maths, catalogs, field models
│   └── Solar_System/           # Scene objects: catalog bodies, Sun/, ring meshes, shells, OrbitLayout
├── resource/                   # Runtime assets
│   ├── planets.catalog.json    # **Canonical** body metadata (edit this)
│   ├── planets.catalog.schema.json
│   ├── planet_manifest.json    # GENERATED staged-loading systems
│   ├── shaders/                # GLSL (preloaded into WASM VFS)
│   ├── textures/               # High-res DDS (hosted at deploy time, not all in git)
│   ├── textures_low/           # Low-res DDS + 4×4 placeholders (committed)
│   ├── textures_mid/           # Mid-tier LOD DDS (placeholders + deploy artifacts)
│   ├── models/, fonts/, icons/, sounds/
│   └── asset-manifest.json     # Inventory + optional SHA-256 checksums
├── web/                        # Vite + TypeScript frontend
│   ├── src/main.ts             # WASM bootstrap, progress UI, asset base URL
│   ├── public/planet_facts.json  # GENERATED explorer facts
│   └── threejs/src/data/orbital-parameters.json  # GENERATED companion orbits
├── scripts/generate-planet-metadata.mjs
├── build-web.sh                # Emscripten build + artifact deploy
└── docs/
    ├── ARCHITECTURE.md         # This file
    └── plans/                  # Testing guide, porting guide, future plans
```

### 2.1 Canonical planet metadata flow

Planet-related data used to live in several hand-edited files. The **single source of truth** is now:

`resource/planets.catalog.json` (schema: `resource/planets.catalog.schema.json`)

```
resource/planets.catalog.json
            │
            ▼
  scripts/generate-planet-metadata.mjs
     ├─► web/public/planet_facts.json              → planet explorer panel (focus bodies)
     ├─► web/threejs/src/data/orbital-parameters.json → Three.js companion
     ├─► resource/planet_manifest.json             → staged loading (WASM)
     ├─► src/Solar_System/OrbitLayoutBodies.generated.inc
     └─► src/Solar_System/BodyCatalog.generated.h  → CatalogBody / moons / tilt

resource/asset-manifest.json  ◄──  web/deploy.py --update-manifest  (fill sha256)
```

| Command | Purpose |
|---------|---------|
| `npm run generate:planet-metadata` (from `web/`) | Regenerate JSON outputs |
| `npm run generate:planet-metadata:check` | CI: fail if outputs drift |
| `node scripts/generate-planet-metadata.mjs --write-checksums` | Fill asset-manifest sha256 for files present on disk |
| `python3 web/deploy.py assets --update-manifest …` | Hash + upload assets |

**Index convention:** focus body indices are **Sun = 0 … Pluto = 9, Ceres = 10, Vesta = 11**, matching `OrbitLayout::Body` and explorer `planet_facts.json`. Do not renumber without updating C++ enum / ephemeris tables. `OrbitLayout::kBodyCount` is the loop bound; a `static_assert` in `OrbitLayout.cpp` fails the build if the generated row count and the enum disagree.

**Do not hand-edit** generated JSON (`planet_facts.json`, `planet_manifest.json`, `orbital-parameters.json`) or the generated C++ headers below. Edit the catalog and re-run codegen.

`OrbitLayout::kBodies[]` (heliocentric compressed-art offset, AU distance, orbital period,
inclination, sidereal rotation) is generated into
`src/Solar_System/OrbitLayoutBodies.generated.inc` and `#include`d by `OrbitLayout.cpp`. Focus-body
row count must stay in lockstep with `OrbitLayout::Body` / `kBodyCount` (Sun=0 … Halley=15).
Outer-system bodies (`OrbitLayout::UsesTrueRadius`, Eris … Halley) are placed at their
ephemeris distance in both scale modes instead of a fixed art radius, so eccentric orbits read.

`src/Solar_System/BodyCatalog.generated.h` is generated from each body's `render` block
(shader flags, ambient factor, texture LOD ids, display names, `earthRadiusScale`,
`axialTiltDegrees`) for planets, dwarf planets, and moons. `CatalogBody` /
`CatalogSatellite` / `CatalogClouds` consume a row directly, so a rocky body whose
only distinguishing features fit in that row needs **no C++ class at all**. Mercury–Pluto,
Ceres, Vesta, Eris, Haumea, Makemake, 1P/Halley's nucleus, the Moon, the Galileans, and the
other currently rendered moons are built
this way. Adding another catalog-only rocky body is: catalog row + textures + `initTag`
on the allowlist (the factory loops catalog systems; `MakePlanetInitFunc` is generic).

**Axial tilt SSOT:** `orbit.axialTiltDegrees` is applied on both the WASM/C++ scene
(`CatalogBody::AdjustToParent` does `Rotate(degrees, Z)`) and the Three.js companion
(`orbital-parameters.json`). Optional `render.artTiltXDegrees` is a Saturn-only art
overlay so the rings keep their established presentation; it is *not* a second IAU
value. This replaced the previous split where C++ used `Rotate(81.2°, X)` for Uranus
vs catalog `−97.8°`, `+28.3°` vs `−28.3°` for Neptune, and no tilt at all for Pluto.

Atmospheres are catalog data too: `render.atmosphere` carries the O'Neil shell numbers
(`oneil`) and, for Earth/Venus/Mars/Titan and the four giants, the physical coefficients (`physical`) that
`tools/atmosphere_lut_baker` turns into `resource/atmosphere/*.ktx2` (§9.2). The flow is
catalog → `generate-planet-metadata.mjs` → `BodyCatalog::kAtmospheres` → baker → LUTs
preloaded into `.data`. The Sun and Saturn/Uranus *ring geometry* remain hand-maintained
(`Sun`, `SaturnRing`/`UranusRing`, `SystemVisuals.h`). Moon motion is
described under § Ephemeris accuracy: Keplerian where the row has measured elements, the
circular `SatelliteOrbit::Offset` / `OffsetXY` otherwise — both as functions of the date.

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
    → DOWNLOADING (async WebResourceFetcher::DownloadWithFallback per asset)
    → pendingDownloads == 0 → initFunc() (e.g. InitEarthSystem()) → READY
```

Two paths skip the distance test: `ActivateNearestPlanetSystems()` starts the
`eagerPlanetSystems` systems nearest the start pose (3 on Low; see `QualityTierSettings`) as
soon as the manifest loads, and `FocusPlanetByIndex()` (UI focus, deep links) starts the focused
body's system immediately instead of when the camera arrives.

`DownloadFile` reports the transfer's own result; a second request for a path already in flight
joins it. `DownloadWithFallback` tries each URL in turn and, when all fail but the path is
resident (the `--preload-file` placeholder tier), reports `KeptResident` instead of `Failed` —
the fetch is still made, because the deployed copy is what replaces the 4×4 placeholder.

While `NOT_LOADED` or `DOWNLOADING`, `RenderPlanetProxyMarkers()` draws 3D labels such as `Earth (approach to load)` or `Earth [downloading 42%]`.

Desktop: all manifests are empty; `InitStarSystem()` creates every planet immediately.

**Manifest file (`resource/planet_manifest.json`):** Preloaded into the WASM bundle alongside shaders, and read from MEMFS — the CDN-overwrite-at-startup path is gone with the blocking `WebResourceFetcher::Fetch` (a hotfix now needs a WASM rebuild, or a `DownloadFile` staged ahead of `LoadPlanetSystemManifests`). Bump the `version` field when publishing an updated manifest so operators can track/cache-bust deployments. Each entry uses `proxyPosition` as an offset from the Sun, `required` / `optional` asset paths, and an `init` tag bound to `InitXxxSystem()` in C++ (`Mercury`, `EarthSystem`, …).

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

After a planet is `READY`, it renders with **low-res** textures (`GetTexturePath` returns `textures_low/` on WASM). Each frame, `UpdateLOD()` calls `planet->LoadHighResIfClose(cameraPos)`, which drives per-map `TextureLODController` instances:

| Condition | Action |
|-----------|--------|
| Distance < T (`_lodThreshold`, medium uses T/1.5) | Queue **mid** textures (`textures_mid/*_Mid.dds`) when preset allows |
| Distance < 0.5×T and preset is **full** | Queue **high** textures (`textures/*`) after mid is resident |
| Distance > T | Downgrade high → mid |
| Distance > 2×T | Downgrade mid → low |
| Preset **low** (`maxTextureLodTier=Low`) | Force low; cancel in-flight upgrades |
| Preset **medium** | Cap at mid (never fetches full 8K) |
| Preset **full** | Allow mid then high |

Loads are deduplicated, cancellable on camera retreat, concurrency-capped by quality preset, and reported through `window.updateStreamingProgress(completed, total, active, tierCode)` (`tierCode`: 0 generic, 1 mid, 2 high).

**Mipmap safety:** every upload path — `nv_dds`, the KTX2 reader, and the software BC decoder — sets `GL_TEXTURE_BASE_LEVEL=0` and `GL_TEXTURE_MAX_LEVEL` to the last uploaded mip level. Incomplete mip chains cause black textures on WebGL 2.

### 4.4 Texture formats and packs

LOD paths are written as `.dds` everywhere and rewritten at load time by `TexturePaths::Resolve()` → `TextureFormats::VariantPath()`, so the tier table above is format-agnostic. Which format is used depends on what the GL context reports:

| Context | Pack picked | What loads |
|---------|-------------|------------|
| Desktop with `EXT_texture_compression_bptc` | `bc7` | `textures_*/bc7/*.ktx2` if published, else `.dds` |
| Desktop Chrome (S3TC) | `bc3` | `.dds` as before, or a `bc3` pack |
| Safari / iOS, modern Android (ASTC) | `astc` | `astc` pack if published, else `.dds` **software-decoded to RGBA8** |
| Older Android (ETC2 only) | `etc2` | `etc2` pack if published, else software decode |
| CI / software GL (nothing compressed) | `rgba8` | software decode |

A deployment opts in by building with `VITE_TEXTURE_PACKS=astc,etc2,bc3` after running `scripts/convert_textures_ktx2.py`; with none published every GPU stays on `.dds`. Quality presets are unaffected — pack selection and LOD tier are independent, so preset 0 still never upgrades past low. The skybox cube map is deliberately excluded (the KTX2 reader is 2D-only).

See `docs/plans/PORTING_GUIDE.md` § 3d for why no Basis/`libktx` transcoder is linked into the Wasm module.

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
    → LoadHighResIfClose(cameraPos) / TextureLODController::Update
    → distance < T → queue textures_mid/*_Mid.dds (medium + full)
    → distance < 0.5T and full → queue textures/*.dds
    → TextureImage2D::ReloadTexture(path)
    → retreat: high→mid→low with hysteresis; cancel in-flight jobs
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
| Atmosphere LUTs (`resource/atmosphere/`, ~560 KB RGBA16F KTX2) | Yes | — |
| Low-res placeholders | Partial (via build copy to `web/public/`) | Core + staged + LOD |
| High-res textures | No | LOD queue + skybox core load |
| Sounds | No (may be absent in checkout) | Optional parallel download |

See [README.md § Runtime asset hosting](../README.md#runtime-asset-hosting) for CDN layout, manifest checksums, and `web/deploy.py` targets. For COEP/CORS/CORP header matrix and subdomain CDN setup, see [docs/CROSS_ORIGIN_HEADERS.md](CROSS_ORIGIN_HEADERS.md).

---

## 7. C++ module map

### 7.1 Core

| Module | Role |
|--------|------|
| `Application` | Scene graph, simulation (`UpdateScene`, once per frame), culling, loading orchestration, quality presets |
| `Renderer` / `FrameContext` / `FramePasses` | Draw passes and the GPU resources they own; each pass reads only its `FrameContext`; `FramePasses` is the GL-free frame order (§7.5) |
| `SystemModules.h` | Platform-conditional GLFW/SDL/audio/FreeType includes; the GL header and the WASM `glBindTextureUnit` polyfill come from `gfx/GlPlatform.h` |

### 7.2 Subsystems by folder

The old `Auxiliary_Modules/` grab-bag is split by what a file depends on. `astro/` and most
of `assets/` are GL-free and compile into the unit tests; `gfx/` owns every GL object
(`scripts/check-gl-handles.sh`); `render/` composes `gfx/` into drawable features.
`scripts/check-source-rules.sh` (CI) keeps `using namespace` out of headers and iostream
logging out of `src/`.

| Folder | Module | Role |
|--------|--------|------|
| `core/` | `Log` | `LOG_*` macros, level from `SOLARSYSTEM_LOG` / `?log=` (§7.6) |
| | `Camera`, `FrameTimer` | First-person camera (double world position); native frame pacing and the FPS count |
| | `CameraNavigation` | GL-free altitude, floor, facet ray cast, clip planes, altitude-scaled speed and anchor hysteresis (§11.1) |
| `gfx/` | `GlHandle`, `GlPlatform.h` | Move-only RAII GL objects (the only `glGen*`/`glDelete*`); the GL header and web polyfills |
| | `Shader` / `ShaderLibrary` | Deferred compile/link (`KHR_parallel_shader_compile`), link-time uniform table with a value cache, hot reload; programs keyed by `ShaderSpec` (files + permutation defines) (§9.1) |
| | `ShaderSource` | GL-free source assembly: `#version` rewrite, `common/preamble.glsl`, permutation `#define`s, `#include`, sampler bindings, `#line` bookkeeping (§9.1) |
| | `FrameUniforms`, `TextureUnits.h` | The per-view `FrameUBO` and the shared texture-unit list (§9.1) |
| | `Mesh`, `MeshHolder`, `Vertex.h`, `LineStripBatch` | Model meshes (with bounding radius and a CPU copy of their triangles) and the one ribbon VAO layout |
| | `MeshAnchor.h`, `SurfacePatch`, `SurfacePatchMesh` | Relative-to-eye vertex placement and the re-tessellated facets under a camera metres up (§11.1) |
| | `TextureImage2D`, `FloatLutTexture` | DDS/KTX2 load, reload, mip level management; baked RGBA16F LUTs (quiet `nullopt` instead of a checkerboard) |
| | `ShadowMapFBO`, `HDR`, `Frustum.h` | Shadow map target; star HDR target; frustum test and fitted depth range (§7.5) |
| | `GlCapabilities` | One probe of extensions and limits (compressed formats, float FBOs, parallel compile) |
| `render/` | `LensFlare`, `TextRenderer` | Star lens flare; FreeType HUD text |
| | `OrbitPathRenderer` | Faint heliocentric `GL_LINE_LOOP` guides |
| | `MissionPathRenderer` | Sampled probe ribbons (Voyager); quality downsample; AU→scene via `HelioAuToScene` |
| | `XrPointerRenderer` | WebXR controller rays (skipped on Low) |
| | `MagneticFieldLineMesh` / `MagneticFieldLineRenderer` / `MagneticFieldBloom` | Optional dipole+toroidal ribbons (static VBO, GPU flow, half-res bloom on Medium+, separate from the star HDR FBO) |
| `assets/` | `WebResourceFetcher` | `DownloadFile` (async, callback-based) + `RequireResident` (residency probe) — WASM only |
| | `TextureLoadingQueue`, `TextureLODController` | Serialized high-res LOD downloads; per-texture tier state machine (§4.3) |
| | `PlanetManifestLoader` | Parses `planet_manifest.json` for staged loading (§4.2) |
| | `TextureFormatSupport`, `BlockCompression` | Texture-pack choice; CPU BC decode when S3TC is missing (§4.4) |
| `astro/` | `Ephemeris` | `IEphemeris` backend: Standish planets, Keplerian Pluto/belt bodies, Keplerian moons, GMST |
| | `SkyEvents` | Conjunction, eclipse, transit, and shadow-transit searches over the active backend; `GeocentricEclipticKm` exposes the geocentric vectors it searches with |
| | `StarCatalog` | Parser for `resource/sky/bright_stars.json` (columnar, J2000, sorted brightest first) and the B−V → colour fit; see §7.4 |
| | `Observer`, `LunarTheory`, `SeatedView.h` | Ground-observer maths: sidereal time, precession, ecliptic→equatorial→horizon frames (X=East, Y=Up, Z=−North), Moon parallax, `ComputeSky` for the Sun, Moon and naked-eye planets, `DiscOverlapFraction`; the XR seated-sky rotation (§7.4) |
| | `MissionCatalog` | Probe trajectory samples (`resource/missions/catalog.json`) |
| | `MagneticFieldParams` / `MagneticFieldModel` / `MagneticFieldTracer` | Per-body field parameters, the dipole+toroidal model and the line tracer behind the ribbons |

**Tests.** `SolarSystemTests` is GL-free (it links GL only for the inline `GlHandle`
destructors, which never run on an empty handle). `SolarSystemGlTests` (`tests/gl/`) opens a
hidden GLFW window with a real GL 4.6 core context and checks handles, that every
planetLighting permutation links and reads `FrameUBO` at its binding, sampler units, the
uniform cache, the shadow/HDR framebuffers, one `LineStripBatch` draw read back with
`glReadPixels`, and a DDS upload that must not fall back. CI runs it under Xvfb/llvmpipe; with no display it skips
(`MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 xvfb-run -a <build>/tests/SolarSystemGlTests`).

### 7.3 Scene objects

Inheritance: `SpaceObject` → `Transformable` → `Planet` / `Satellite` / `Star`.

Planets and moons are catalog rows (`CatalogBody`, `CatalogSatellite`, `CatalogClouds`), so there is no per-planet folder any more: the only subfolders left are `Sun/` and the two ring meshes, `Saturn_System/SaturnRing` and `Uranus_System/UranusRing`. `SolarSystem.h` aggregates the scene-object includes. Atmospheres, clouds, and rings are separate render components. `Atmosphere` is configured from its catalog row and owns its LUTs; `AtmosphereModel` is the GL-free physics behind them, shared with the baker and the tests (§9.2).

**Drawing.** A scene object holds no program: a pass `Use()`s one, sets its own uniforms
and calls `SpaceObject::Draw(shader)`, which adds `model` and the object's material maps.
So the shadow pass and the colour pass draw the same body with different programs, and a
catalog body's colour pass uses its own planetLighting permutation (§9.1).
`GetBoundingSphere()` is the mesh's radius (`MeshHolder::BoundingRadius`) under the current
model matrix, in the render frame.

**Magnetic field params:** `MagneticFieldParams` on `SpaceObject` (set in `StarSystemFactory.cpp` via `MagneticFieldCatalog::IntrinsicParamsForBody`). Values are visual/educational — not SI magnetosphere physics. `ParamsForBody(body, quality)` adds seed/sample scaling and quality enable gates for the ribbon renderer. `Renderer::RenderMagneticFields` walks the Sun, loaded planets, and satellites whose `enabled` flag is set. Satellites default to disabled. Magnetic mode (`SetMagneticFields` / alias `SetMagneticFieldMode`, settings **M**) dims planet/cloud/atmosphere/ring shaders via `uSurfaceDim` and fades orbit paths; the Sun is left at full brightness.

### 7.4 Observe mode

Observe puts the camera on Earth at a lat/lon and a UTC instant (`?mode=observe&lat=&lon=&jd=`, the **Observe** toggle, or `O` natively). It is a separate pass, not the scene drawn from a different camera, because scene scale cannot give true angular sizes: the Sun mesh is ~9× too small and the Moon ~5× too big, and the skybox cubemap's frame against J2000 is undocumented. `OrbitLayout::ScaleMode` is left alone.

| Piece | Role |
|-------|------|
| `astro/Observer.*` | Pure maths. Never reads the Earth model's matrix (its pole and prime meridian are art); goes ecliptic J2000 → equatorial J2000 → equatorial of date (precession) → horizon (local sidereal time, geodetic latitude). Moon parallax uses the WGS84 observer vector; Sun and planets are geocentric. |
| Moon orientation | `Observer::MoonBodyAxesEquatorialJ2000` is the IAU 2009 WGCCRE lunar orientation (pole α0/δ0 and prime-meridian angle W with the E1–E13 terms), as right-handed body axes (X lon 0, Y lon +90 east, Z north). `ComputeSky` carries them into the horizon frame as `moonBodyToHorizon` and reports the sub-Earth point (libration). The fragment shader turns each disc point's topocentric surface normal into selenographic lon/lat with the transpose, so parallax and libration are already in it. Tests pin the frame by behaviour: right-handedness, a pole near (270°, 66.5°), longitude libration following the orbit's equation of centre (corr > 0.9, within ~10°), and latitude libration opposite the Moon's ecliptic latitude. The map is sampled as equirectangular, centre lon 0, north at the top (`MOON_MAP_CENTRE_LON` in `observeSprite.fs` if a deployed texture differs); DDS textures are loaded unflipped here, so t = 0.5 − lat/π. The texture comes from the loaded Moon `CatalogSatellite` (`GetDiffuseTexture()`, which changes on LOD reloads), falling back to the plain lit disc until its system has loaded; Observe keeps Earth's component re-placed each frame so the LOD manager sees the Moon as near. |
| `ObserveState.h`, `ObserveMode.cpp` | State on `Application` (not `SimState`, which the tests link): site, time rate, saved Explore camera. `SetObserveMode` locks camera movement (`Camera::SetMovementLocked`, which also covers touch and the XR stick), widens the zoom range to 0.25–90° with sky-style zoom (`Camera::SetSkyZoom`: a scroll notch scales the FOV by a ratio, and look sensitivity scales with FOV / 60°, so a telescope field handles like the naked eye), and restores the Explore pose on exit. Focus keys, `FocusPlanetByIndex`/`FocusMissionByIndex` and `SetCameraPose` are ignored while active. `GetObserveStateJson` is the JS-facing snapshot. |
| `Solar_System/ObserveSky.*`, `shaders/observe*.{vs,fs}` | The sky pass: an analytic sky/ground gradient from the Sun's altitude, additive point sprites for planets, an additive Sun disc with limb darkening and halo, and an alpha-blended lit Moon disc. Stars are one static instance buffer of J2000 unit vectors, rotated into the horizon frame by a single matrix uniform (`Observer::HorizonFromEquatorialJ2000`); their size and brightness come from `LimitingMagnitude(sunAlt)` in the vertex shader, so they fade in through twilight. Stars and planets are fixed-size screen-space points (round out to the edge of a wide FOV); the Sun and Moon are true angular discs on the tangent plane. A planet becomes a tangent-plane disc too once its true radius (`SkyBody::angRadiusDeg`, IAU equatorial radius over geocentric distance; Saturn without rings) reaches 2 px and half its point sprite — in practice only in a telescope field — lit from the Sun's side using the phase angle and bright-limb angle packed into the instance (`params.zw`), so Venus shows its crescent. `tangentBasis` in `ObserveSky.cpp` must match the vertex shader's basis for that angle to land. Camera at the origin, no depth buffer, drawn back to front. Per-instance varyings are `flat` — an interpolated float `kind` will occasionally round to the wrong branch. |
| `resource/sky/bright_stars.json` | The 5000 brightest Yale BSC5 stars (public domain), generated by `scripts/generate-star-catalog.mjs --input <catalog>` (the raw catalogue is not committed; `--check` validates the committed file, or compares against `--input`). Columnar `[ra, dec, vmag, bv]` in J2000 degrees, sorted by magnitude so a quality tier draws a prefix (`observeStarCount`: Low 500, Medium 2000, Full 5000). Preloaded for web (`--preload-file resource/sky`) and read lazily on the first Observe frame. Proper motion is ignored. |
| `ObserveRenderer.cpp` | `Renderer::RenderObserveSky` (view/projection, XR eyes) and `RenderObserveHud` (compass, body names, readout, disclaimer). |

**Seated sky in a headset.** `RenderXrStereoFrame` already calls `RenderFrameContent` per eye, so Observe needs only the sky pass to accept the eye matrices. `SeatedView::ViewRotation` (header-only, unit-tested) is `mat3(eye.view) * rotateY(heading)`: the headset supplies pitch, roll and yaw inside the room, and a single rotation about the vertical — the azimuth the room's forward currently faces, which is the camera yaw + 90° — ties that to the horizon frame. Translation is dropped (the sky is at infinity) and the camera's pitch is ignored (it is the viewer's own head). A level head reproduces the flat-screen camera exactly (test). `ObserveSky` already sizes stars and planets in pixels from the eye's viewport and projection, and the world-anchored compass/body/star labels are drawn per eye through `RenderObserveLabels`, scaled with the eye's resolution; the 2D readout is not drawn in a headset. In `webxr.ts`, while Observe is active the sticks stop flying: the left stick scrubs time (quadratic, up to 3 simulated hours per second, via `setSimulationEpoch`), the right stick snap-turns the heading by 30° (via `setObserveView`), `setTouchMovement` is held at zero, and the XR tooltip says so. Not exercised in CI (the Enter VR button stays hidden without `navigator.xr`); the maths is covered by `tests/test_seated_view.cpp`.

The epoch is still `OrbitLayout::GetJulianDate()`, so the date controls and `SetSimulationEpoch` keep working; in Observe the frame loop advances it at wall-clock UTC × `timeRate` (instead of the orrery's 3 days per second) and honours Pause. The scene camera is parked just above Earth each frame so staged loading, LOD and the nearest-planet search behave as if the viewer were there; scene components are not drawn or re-placed while Observe is active, so their positions are stale until Explore resumes.

**Telescope.** Not a separate mode: a field of view at or below 2° (`TELESCOPE_MAX_FOV_DEG` in `observe.ts`). The panel's **Telescope** select offers 2° / 1° / 0.5° / 0.25° eyepieces and "Naked eye" (back to the wide field in use before); scroll and pinch reach the same range. While the FOV is in that range an HTML overlay (`#telescope-overlay`, pointer-events off) draws a circular field over the canvas's short side with a faint crosshair and the field across the circle. No optics are simulated: no magnitude gain, no inverted image, no Galilean moons or rings yet.

Web: `web/src/observe.ts` (panel, sites, rates, 2017 preset, eyepieces and the telescope overlay, `ObserveState` parsing), `deepLink.ts` (`mode`, `lat`, `lon`, `az`, `el`, `fov` 0.25–120, `rate`; Observe angles keep 0.01° so a telescope link still lands on its target; an Observe share link omits the fly-through pose), exports `SetObserveMode`/`SetObserverSite`/`SetObserveView`/`SetObserveFov`/`SetObserveTimeRate`/`GetObserveStateJson`. Accuracy is visualizer-grade (§11): UTC is fed straight into GMST, nutation and refraction are ignored, and the Moon comes from the Meeus ch. 47 series (§11), so eclipses land within seconds of NASA's circumstances.

---

### 7.5 Frame pipeline

One frame (`Application::RenderFrame` → `FramePasses::Run`):

1. **Simulate once** (`UpdateScene`): orbit placement, light frusta, belts, mission probes,
   LOD, the nearest-planet search, sky events. XR does this once for both eyes.
2. **Snapshot** (`BuildFrameSnapshot`): one `CameraView` per view and a `SceneSnapshot`; the
   passes see the frame only through a `FrameContext` built from them.
3. **Cull** (`CullScene`): each object's bounding sphere against the side planes of every
   view's frustum (`gfx/Frustum.h`). A component no view sees gets neither a shadow nor a
   colour pass — its shadow map only shades its own bodies — except the nearest one, whose
   colour pass is where the star is drawn. Visible components are drawn front to back.
4. **Draw**: background per view; then per component, its shadow pass followed by each
   view's colour pass (one shadow FBO); then the foreground per view.

**Shadow frustum.** Each component's light frustum is an ortho box of ±3 planet radii
looking from the Sun at the planet, with its depth range fitted to the component's own
bodies (`FitDepthRange`): the only casters drawn into that map and the only receivers that
read it. `shadowDepthRange` (near, far − near) goes to the shaders that turn a stored depth
back into a distance (the ring tests), and each pass's `bias` is a fixed scene-unit bias
divided by that range, which keeps the comparisons the shaders always made. (The frustum
used to borrow the camera's 0.001–20000, which also clipped every body more than 20000
units from the Sun — Uranus and beyond on realistic scale.)

At debug level (§7.6) the `[FrameStats]` line logs, every N frames (`SOLARSYSTEM_FRAME_STATS=N`,
120 by default), the previous frame's simulate count, view binds, shadow passes, component and
body draws, culled components and `glUniform*` calls.

### 7.6 Logging

All diagnostics go through `src/core/Log.h`: `LOG_DEBUG/INFO/WARN/ERROR(category, fmt, ...)`
with a printf-style format, which is not evaluated when the level is off. A line reads
`[Category] message`, so the smoke test's and CI's greps (`[StagedLoading] Loaded manifest`,
`[ShaderSelfTest] … OK`) match on both targets.

| | Native | Web |
|---|---|---|
| Sink | stderr, `HH:MM:SS.mmm LEVEL [Category] …` | `emscripten_log` → `console.debug/info/warn/error` |
| Level (default `info`) | `SOLARSYSTEM_LOG=debug\|info\|warn\|error\|off` | `?log=debug` (same names) |
| GL driver messages | KHR_debug context in Debug builds (`SOLARSYSTEM_GL_DEBUG=1/0` forces it on/off); `glDebugMessageCallback` maps severity to level as `[GL] 0x<id>: …`, at most 5 lines per id | — |

Per-file chatter (each texture loaded, each download started or finished, LOD queueing) is
`debug`. A failure the app recovers from (fallback texture, missing optional asset) is `warn`,
and only a broken invariant (a required asset that is missing, an incomplete framebuffer, a
shader that fails to compile) is `error`. That keeps `console.error` meaningful for the smoke
test's error filter. Use `LOG_*` rather than `std::cout`/`std::cerr`. The parsers that used to
`catch (...)` around `std::stod` now use `strtod` and report a malformed number as a parse error.

## 8. Web frontend (`web/`)

| File | Role |
|------|------|
| `index.html` | Canvas, loading overlay, settings panel, streaming progress bar, tour caption, XR HUD |
| `src/main.ts` | Module init, `updateLoadingProgress`, `updateStreamingProgress`, `setCameraPose`, settings persistence |
| `src/tourPlayer.ts` | Guided-tour playlist over `SolarSystemRuntime` |
| `src/educationalLayer.ts` | Mission list + tour controls |
| `src/observe.ts` | Explore/Observe switch, site + UTC time + rate controls, 2017 eclipse preset, telescope eyepieces + overlay, `ObserveState` parsing |
| `vite.config.ts` | Base path `/solar-system/` |
| `public/SolarSystem.{wasm,data}` | Generated by `./build-web.sh` |

Rebuild WASM after C++ changes: `./build-web.sh` then refresh the browser.

---

## 9. Platform differences (quick reference)

### 9.1 Shaders

| Aspect | Native | Web |
|--------|--------|-----|
| Version in `resource/shaders/` | `#version 300 es` (single source) | `#version 300 es` |
| Version actually compiled | `#version 460 core` (rewritten at load) | `#version 300 es` |
| Precision | Qualifiers accepted and ignored | `common/preamble.glsl` sets `highp` defaults in fragments |
| `#include "common/x.glsl"` | Expanded by `ShaderSource` | Expanded by `ShaderSource` |
| Geometry/compute shaders | Geometry allowed | **Not supported** — no 3-arg `Shader` ctor exists |
| Parallel compile | `KHR`/`ARB_parallel_shader_compile` via GLEW, if the driver has it | `KHR_parallel_shader_compile` WebGL extension, if exposed |
| Hot reload | `resource/shaders/` mtime poll every 0.5 s (`SOLARSYSTEM_SHADER_HOT_RELOAD=0` turns it off) | Dev server only: `await window.__solarSystem.reloadShaders()` |

**One source, two dialects.** Every file in `resource/shaders/` is written once in GLSL
ES 3.00, because that is the only dialect WebGL 2 accepts. The native build runs an
OpenGL 4.6 **core** context, which accepts `#version 300 es` only through
`ARB_ES3_compatibility` — widely implemented, but not guaranteed. So `ShaderSource`
(`src/gfx/ShaderSource.cpp`, called by `Shader.cpp`) rewrites the leading
`#version 300 es` directive to `#version 460 core` on native builds only (GLSL ES 3.00 is a
subset of desktop GLSL 4.60, precision qualifiers included).

**Preamble and includes.** Every stage is assembled the same way on both targets:

```
#version …                         line 1 of the file (rewritten natively)
#define SS_STAGE_VERTEX|FRAGMENT 1, SS_GLSL_ES|SS_GLSL_DESKTOP 1
#define <each permutation define> 1   e.g. HAS_CLOUDS; from the program's ShaderSpec
#line 1 1   common/preamble.glsl   precision defaults only; no helper macros
#line 2 0   the file itself        on-disk line numbers
            #include "common/x.glsl" → #line 1 <id> … #line <next> <parent>
```

Includes resolve against `resource/shaders/` (preloaded on web), each file is expanded at
most once per stage (no include guards), and cycles, missing files, absolute paths and a
`#version` inside an include are errors. Compiler errors read `<source string>:<line>`; a
failed compile prints the id → file legend and the assembled listing. Shared helpers live
in `resource/shaders/common/`: `raytrace` (sphere/plane/disk), `ring` (ring crossing and
opacity), `shadow_pcf` (`PCF_NUM_SAMPLES` overridable), `eclipse`, `phase`, `tonemap`,
`log_depth`, `noise_simplex3d`/`noise_simplex4d`, and `atmosphere_lut`. They take samplers
and values as parameters and declare no uniforms; each shader keeps its own
`CalculateShadow` wrapper. `tests/test_shader_source.cpp` preprocesses every real shader
and fails on a function defined twice.

**Self-test.** `SOLARSYSTEM_SHADER_SELFTEST=1 ./build/SolarSystem` compiles every `.vs`/`.fs`
in both dialects (Mesa's core context also accepts `300 es`), once per subset of the
defines a stage lists on its `// permutations:` line, prints `[ShaderSelfTest] N/N … OK`
and exits; the native CI job runs it under Xvfb with `MESA_GLSL_VERSION_OVERRIDE=460`.

**Permutations.** A stage that branches on a per-material switch tests a `#define` instead
of a `uniform bool`, and names its switches on a `// permutations: A B …` line
(`planetLighting.fs`: `HAS_NIGHT_TEXTURE HAS_SPECULAR_MAP HAS_CLOUDS USE_SPHERE_INTERSECT`).
A program is identified by its `ShaderSpec` — stage files plus sorted defines — and
`ShaderLibrary` (`src/gfx/ShaderLibrary.h`) builds each spec once.
`tests/test_shader_source.cpp` checks that the declared list matches
`CatalogMaterial::Defines()`, that each switch is tested, and that every subset preprocesses.

**Compiling.** `Shader::Submit` preprocesses, compiles and links without asking for any
status; `Finish()` (or `ShaderLibrary::Get`) checks the logs and builds the uniform table.
`Renderer::Init` prefetches every program it owns — including every planetLighting variant a
catalog row can reach (`CatalogMaterial::PossibleVariants`) — before waiting on the first, so
with `KHR_parallel_shader_compile` (`GlCapabilities::parallelShaderCompile`) the driver
compiles them side by side. `ShaderLibrary::GetIfReady` returns a program only once
`GL_COMPLETION_STATUS_KHR` says it is done, so a variant picked mid-frame never stalls it.
Optional effects use `TryGet` (null when the compile failed); the corona volume and the LUT
atmosphere fall back that way.

**Hot reload.** Every live `Shader` is in a registry with the files it was assembled from
(includes and the preamble too) and their mtimes. `Shader::ReloadChanged()` rebuilds the
ones whose files moved: in place, so references stay valid, and with every uniform value
it had replayed onto the new program (one-off values set at init survive). A program that
fails to compile keeps its previous build and the error is printed. Native polls it from
the frame loop every 0.5 s. On web, `npm run dev` serves `../resource/shaders` at
`<base>__dev/shaders/` (`vite.config.ts`), and `window.__solarSystem.reloadShaders()`
(`web/src/shaderReload.ts`, only imported in dev) re-fetches the files, writes the changed
ones into MEMFS (the module exports `FS` for this) and calls the `ReloadShaders` export.

**No geometry stage on web.** WebGL 2 has no geometry shader, so the `Shader` constructor
overload taking a `geometryPath` is compiled out under `__EMSCRIPTEN__`: passing one in a
web build is a compile error, not a shader that silently fails to link.

**Material binding.** `planetLighting` material state for every catalog body goes through
`CatalogMaterial` (`src/Solar_System/CatalogMaterial.h`). `CatalogBody` and
`CatalogSatellite` build a `CatalogMaterial::Material` from their row's `render.shaderFlags`,
`useSphereIntersect`, and `ambientFactor`. The flags are not uniforms: `ShaderVariant()`
turns them into a variant mask (a map counts only once its texture is resident), the
Renderer draws the body with that permutation (`Renderer::PlanetProgram`), and
`BindMaterial` sets `ambientFactor` and binds the maps. So no body can inherit the previous
draw's clouds or night map, and a body without one never runs that branch.
`generate-planet-metadata.mjs` rejects a row whose `shaderFlags` disagree with its `lod`
ids, and a moon row that asks for night or cloud maps. **A new rocky body needs only a
catalog row, not a `Render()`.**

**Texture units.** Every sampler names its unit in the shader —
`layout(binding = TU_SHADOW_MAP) uniform sampler2D shadowMap;`. The `TU_*` values are the
X-list in `src/gfx/TextureUnits.h`; `scripts/generate-texture-units.mjs` writes them to
`common/texture_units.glsl` (included by the preamble; CI checks it is current). GLSL ES
3.00 has no sampler `binding`, so `ShaderSource` strips the qualifier and `Shader` sets the
unit once at link; C++ binds with `glBindTextureUnit(Unit(TextureUnit::X), texture)` and
never `SetInt`s a sampler. The units are fixed across programs (diffuse 0, normal 1,
specular 2, night 3, clouds 4, shadow map 5, ring 6, the two atmosphere LUTs 7/8, Observe's
Moon 9), so the shadow map is bound once per view, not per program.

**Per-frame uniforms.** View/projection matrices, the eye and Sun positions, near and far
planes, the log-depth `zCoef` and `logDepthC` (§11.1), time and exposure are one std140
block, `FrameUBO`
(`common/frame.glsl`, read as `frame.view` etc.; C++ layout `gfx/FrameUniforms.h`).
`Renderer::BeginView` uploads it once per view (per eye in XR) to binding 0; `Shader` binds
the block of every program that declares it at link. Every lit program reads it (planets,
moons, clouds, rings, atmospheres, the Sun, the asteroid belt and comet tails); the
screen-space and overlay programs (HUD text, lens flare, skybox, orbit/mission/field-line
ribbons) still take their own matrices.

Special cases that stay outside the catalog material path:
- `Sun`: star, corona, and glow programs.
- `SaturnRing` / `UranusRing`: ring meshes on `planetaryRingLighting`.
- `CatalogClouds`: cloud shells on `cloudsLighting.fs`, with their own `cloudsNormalMap`
  binding.
- `Atmosphere`: the O'Neil and LUT programs, configured from `render.atmosphere` (§9.2).
- Asteroid field, comet tails, and the orbit/mission/magnetic-field overlays.

**Uniform locations:** after linking, `Shader` reads the program's active uniforms into a
sorted name → slot table, so `Set*(std::string_view)` costs a binary search and no GL query
or allocation; a name the program does not have is a silent no-op. Each slot keeps the last
value set, and an unchanged value skips the `glUniform*` call. A setter on a program that
is not current binds it for the call and restores the current one, so the cache stays true.
The debug-level `[FrameStats]` line (§7.5) includes the `glUniform*` count.

### 9.2 Atmospheres and the Sun

**Two atmosphere paths, one catalog row.** `render.atmosphere.oneil` drives the O'Neil
single-scattering shell (`atmosphere.fs`), which every preset can afford and which is the
only path for Pluto. Bodies whose row also has `physical`
(Earth, Venus, Mars, Titan, Jupiter, Saturn, Uranus, Neptune) switch to `atmospherePbr.fs`
on Medium/Full:

| Step | Where | What |
|------|-------|------|
| Bake (offline) | `tools/atmosphere_lut_baker` + `AtmosphereModel` | Transmittance LUT 256×64 (Bruneton 2017 mapping) and multi-scattering LUT 32×32 (Hillaire 2020), double precision → RGBA16F KTX2 with a parameter hash in the key/value data |
| Ship | `resource/atmosphere/<id>_{transmittance,multiscatter}.ktx2` | Preloaded into `.data` (~140 KB per body, ~1.1 MB for all eight; `CommittedLutsFitSizeBudget` caps it at 1.2 MB) |
| Load | `LoadAtmosphereLuts` (`StarSystemFactory.cpp`) → `FloatLutTexture` | Always loaded, so preset changes need no reload; any failure leaves the body on O'Neil |
| Draw | `Renderer::RenderPbrAtmosphere` | One view ray per shell fragment, `uSteps` samples (8 Medium / 16 Full / 12 mobile Full) warped toward the limb; sun transmittance and Ψ_ms from the LUTs |

The shader's output is premultiplied — rgb in-scattered light, alpha the luminance of the
view transmittance — and blended with `GL_ONE, GL_SRC_ALPHA`, so the planet seen through
the limb is attenuated, not just tinted. The cloud shell is drawn *before* a LUT
atmosphere for the same reason. The planet's own shadow is exact per sample (a sun ray
that hits the ground gets no direct light); ring shadows and a moon's umbra
(`common/eclipse.glsl`) are applied analytically; the shared shadow map is not used on
this path because it also contains the planet itself. There is no separate
aerial-perspective pass: surfaces are spheres without displacement, so the shell's view ray,
clipped at the ground sphere, already carries the in-scatter and attenuation for the
terrain behind it — from orbit and from inside the shell alike.

**Giant planets.** The giants use the same model and baker; only their numbers differ.
The "ground" is the cloud deck at the 1-bar *equatorial* radius (the radius the mesh is
drawn at), with a Lambert albedo in the deck's colour for the multiple-scattering bounce.
Rayleigh is H₂ scattering for the column above the visible cloud tops, which on Jupiter and
Saturn is a fraction of the 1-bar column, so it reads as a faint blue limb rather than a blue
sky over the disc. The Mie layer is the haze: Jupiter's and Saturn's chromophore haze
absorbs blue (single-scattering albedo falls from red to blue), and Uranus's and Neptune's
haze scatters green and blue alike while the absorption layer — a tent centred on the deck,
i.e. CH₄ thinning with height — takes out red, which gives the pale ice-giant limb instead
of a λ⁻⁴ Rayleigh blue (pinned by `test_giant_atmospheres.cpp`). `thicknessScale` (2–6)
widens each shell to 2–3 % of the radius.

**Oblateness.** `render.oblateness` (flattening, 1 − polar/equatorial: Jupiter 0.065,
Saturn 0.098, Uranus 0.023, Neptune 0.017) squashes the surface (`CatalogBody`), the cloud
shell (`CatalogClouds`) and the atmosphere shell (`Atmosphere`) along the pole with one
helper, `BodyShape.h`, after the tilt and before the spin. Both atmosphere shaders take
`bodyToSphere` (and the LUT shader `sphereToBody`), the linear map from scene offsets to the
frame where the figure is a sphere of the equatorial radius, and trace the view ray, the
Sun direction and the camera there, where the LUT parameterisation holds. Ring and eclipse
tests map samples back to scene space. Optical depth is stretched by up to 1/(1 − f) toward
the poles; the limb follows the deck, which is what is visible. Spherical bodies get the
identity, and `Atmosphere::Contains` makes the camera-inside test oblate-aware.

**Ring shadows on a giant's air.** A moon's atmosphere (Titan) tests the ring once per
fragment: the ring is far larger than the air. On the ringed planet itself
(`isRingShadowPerSample`, Saturn and Uranus) the shadow is a band across the disc, so each
view-ray sample tests its own sun ray against the ring annulus. The ring normal is fixed in
the scene and the Sun direction comes from the planet's position on its orbit, so the
opening angle follows the date. In the scene's geometry (catalog tilt plus Saturn's art
tilt, not the real pole), Saturn's rings are open on **2008‑01‑01** (B ≈ +29°) and edge-on in
April 2030; Uranus's are open on **2024‑01‑01** (B ≈ −38°) and edge-on in February 2033.
`GiantRingShadow.*` checks that at those epochs the band falls on the hemisphere facing away
from the Sun and collapses when the rings are edge-on.

**Cloud shells under the air.** Jupiter and Saturn have no separate cloud shell: the
diffuse map is the deck. Uranus's and Neptune's shells sit 0.3 % inside the mesh radius,
under the LUT ground sphere, so the view ray stops at the surface above them; drawn first
(as for Earth), they are attenuated with the planet.

**Comparing captures.** Native `SOLARSYSTEM_CAMERA_POSE=x,y,z,yaw,pitch[,jd]` applies the same
pose as the web's `window.setCameraPose(x, y, z, yaw, pitch)`; the optional Julian date pins
and pauses the clock so planet positions match. `SOLARSYSTEM_SCALE=realistic` switches scale
first (§11.1). `exposure` is a display uniform, not part of
the bake, so a native and a web capture at one pose differ only by the display.

`thicknessScale` stretches a shell while keeping vertical optical depth, for bodies whose
real atmosphere would be a sub-pixel line; `exposure` is a display setting applied in the
shader and is not part of the bake. **No float render target is involved** —
`EXT_color_buffer_float` only gates HDR/bloom, and sampling RGBA16F with linear filtering
is core WebGL 2 — so Low/mobile fall back by preset, not by capability.

**After editing `physical`:** `node scripts/generate-planet-metadata.mjs`, then build the
`atmosphere_lut_baker` target (`-DSOLARSYSTEM_BUILD_TOOLS=ON` or `…_TESTS=ON`) and run
`atmosphere_lut_baker --out resource/atmosphere` from the repo root. The
`AtmosphereLutsFresh` ctest re-bakes and fails on a stale hash or values that drift beyond
half-float rounding. Changing a mapping or table size means bumping `kLutMappingVersion`
and `ATMO_LUT_MAPPING_VERSION` together (pinned by `test_atmosphere_model.cpp`).

**Sun.** `star.fs` applies power-law limb darkening per channel (μ^α, α ≈ 0.40/0.50/0.65
for R/G/B) to the granulation/sunspot surface. On Medium/Full the corona is
`starCoronaVolume.*`: `coronaSlices` instances (16/32) of the glow quad, each a slice
perpendicular to the camera→Sun axis through a 6 R☉ sphere, evaluating a Baumbach K-corona
density with 4D-noise streamers and discarding points behind the photosphere. Inside the
corona (within ~1.2 × 6 R☉, with hysteresis) the slices turn to face the view direction
instead, so rays at the screen edge do not graze them; in both cases they span only the
part of the sphere in front of the camera, starting just past the near plane and fading in
there. Inside the photosphere the corona is skipped. It is drawn after the planets so they
occlude it. Low keeps the flat `starCorona` billboard. The 2D
`starGlow` + HDR composite and `LensFlare` are unchanged and stay skipped in XR.

| Preset | Atmosphere | Corona |
|--------|-----------|--------|
| Low (desktop + mobile) | O'Neil | billboard |
| Medium | LUT, 8 steps | 16 slices (mobile: billboard) |
| Full | LUT, 16 steps (mobile 12) | 32 slices (mobile 16) |

**XR.** None of these passes binds a framebuffer; camera positions come from the current
eye's view matrix (`Renderer::CameraWorldPosition`). Any future offscreen pass must return
to `Application::DefaultFramebuffer()`, never FBO 0 — under WebXR the frame composites into
the `XRWebGLLayer`'s framebuffer.

### 9.3 Threading

| Task | Native | Web |
|------|--------|-----|
| Background music | `std::thread` | `UpdateBackgroundMusic()` per frame |
| Nearest-planet search | `UpdateSearchNearestPlanet()` every frame, main thread | same |

### 9.4 Texture path helper

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


### 9.5 Diagnostics

| Aspect | Native | Web |
|--------|--------|-----|
| Log sink | stderr with `HH:MM:SS.mmm LEVEL` | `console.debug/info/warn/error` via `emscripten_log` |
| Log level | `SOLARSYSTEM_LOG=debug` | `?log=debug` |
| GL driver messages | KHR_debug in Debug builds (`SOLARSYSTEM_GL_DEBUG=1/0`) → `[GL]` log lines | Browser console (WebGL warnings) |
| Frame counters | `[FrameStats]` at debug level (§7.5) | same |
| Uncompressed DDS upload | Sized internal format (`GL_R8`/`GL_RGB8`/`GL_RGBA8`); core profile rejects the legacy component count | `GL_R8`/`GL_RGBA8` (RGB expanded to RGBA for mipmapping) |

## 10. Adding a new planet

**Catalog-only bodies first:** if the world needs nothing but a diffuse/normal (optional
specular) texture set, an orbit, and catalog tilt, skip the list below — add a catalog entry
with a `render` block, textures, and an `initTag` on `initTagAllowlist`. Codegen updates
`BodyCatalog.generated.h`; `InitCatalogSystem` / `MakePlanetInitFunc` pick it up with **no new
`.h/.cpp`**. If it is a new *focus* body, also add an `OrbitLayout::Body` value, a
`BodyFromName` mapping, and Keplerian elements in `Ephemeris.cpp` if it is not in the Standish
table. A **moon** gets its Keplerian elements from its own catalog row
(`orbit.keplerian`, including the `OmegaDotDegPerDay` / `omegaDotDegPerDay` secular rates)
rather than from `Ephemeris.cpp`; it is only placed from those elements once its id is added
to `kKeplerianSatellites` in `Ephemeris.cpp`, which is the gate that keeps rows still carrying
placeholder angles on the old circular `SatelliteOrbit::Offset` path. See § Ephemeris accuracy. Keep focus indices **0–11 frozen** (Sun=0 … Vesta=11). The outer system appends after them:
**12=Eris, 13=Haumea, 14=Makemake, 15=1P/Halley** (kind `comet`). Satellites are numbered
*after* the last focus index (Moon=16 … Charon=36) — the generator rejects a satellite index at
or below it — so adding a focus body shifts the satellite ids, not the focus ids. Satellite ids
are internal (Ephemeris / SkyEvents / tests), never in URLs or the JS API.

**Atmosphere:** add a `render.atmosphere` block to the row (planet *or* moon). `oneil` alone
gives the cheap shell; add `physical` and rebake (§9.2) for the LUT path. No C++ change.

The steps below are for bodies with rings or shaders that do not fit `CatalogBody` flags
(today: the Sun; Saturn/Uranus ring *meshes*).

1. Add ring numbers to `src/Solar_System/SystemVisuals.h` if needed.
2. **Desktop:** `InitStarSystem()` already loops catalog initTags.
3. **WASM:** staged loading reads `planet_manifest.json` (generated); `MakePlanetInitFunc` maps
   any allowlisted initTag to `InitCatalogSystem`.
4. **LOD:** `CatalogBody` already wires diffuse/normal/specular LOD from the row.

---

## 11. Ephemeris accuracy and the eclipse path

Everything positional hangs off one interface, `Ephemeris::IEphemeris`. `Ephemeris::Position`
answers heliocentric questions (Standish Table 1 for Mercury–Neptune; fixed Keplerian elements
for Pluto, Ceres, and Vesta) and `Ephemeris::SatellitePosition` answers parent-relative ones for
moons. `SkyEvents`, `OrbitLayout`, and `CatalogSatellite` all read through those two calls, so
swapping in a different backend — a truncated VSOP87D, say — moves every consumer at once.

**The Moon** is the exception to everything below: `StandishEphemeris::SatelliteParentRelative(16)`
returns `LunarTheory::MoonGeocentricJ2000Au`, the Meeus ch. 47 series (60 longitude/distance and 60
latitude terms, plus the Venus/Jupiter/flattening additives; the tables are generated from a public
transcription of Meeus's, cross-checked against his worked Example 47.a to 2e-6°). The series is in
TT and referred to the mean equinox of date, so the backend adds `Ephemeris::DeltaTSeconds` (Espenak
& Meeus polynomials; the ~69 s of the 2020s moves the Moon by 0.01°) and `EclipticOfDateToJ2000`
carries it to the J2000 ecliptic (Meeus 21.5, tested against an independent route through the mean
obliquity and `Observer::PrecessionJ2000ToDate`). The catalog row's `keplerian` block is now only
the *scene* size reference (`aKm`) — `SatelliteOrbit::EphemerisOffset` still rescales the real
distance onto `sceneOrbitRadius`, so the scene Moon's distance now swings between perigee and
apogee like the real one. Measured against NASA's 2017-08-21 circumstances: geocentric greatest
eclipse within ~1 s, totality at Casper, WY 17:42:27–17:45:00 (published 17:42:36–17:45:02). Two
corrections make that so, and both are easy to lose: the Sun's apparent longitude is the geometric
one minus 20.4898″/R (`SkyEvents::geocentricKm`, Meeus ch. 25 — without it every solar eclipse is
~36 s late), and ΔT is applied to the Moon only (the planets move too slowly for it to show).

**Other moons.** A satellite is placed from its catalog `orbit.keplerian` row only when its index
is listed in `kKeplerianSatellites` (`Ephemeris.cpp`): today Io, Europa, Ganymede, Callisto,
Titan, and Triton. Everything else still uses the circular `SatelliteOrbit::Offset` /
`OffsetXY` helpers, because its row's node and periapsis angles are placeholder zeros and a
confidently wrong orbital plane is worse than an obviously simplified one. `SatelliteOrbit::
EphemerisOffset` rescales the AU vector so the semi-major axis lands on the catalog
`sceneOrbitRadius` — the orbit keeps its real shape and tilt, not its real size, the same art
compression `OrbitLayout` applies to the planets.

Both paths are **functions of `OrbitLayout::GetJulianDate()`**, never per-frame integrators.
The circular path uses `SatelliteOrbit::MeanAnomalyAt` (`initialAnomalyRad` at J2000, one
turn per `orbitalPeriodDays`), and moon spin and `CatalogClouds` spin use
`SatelliteOrbit::SpinDegreesAt` on the same 120 s-per-year clock `OrbitLayout::Advance` runs.
A `SetSimulationEpoch` jump therefore moves every moon to the pose that date implies, and
scrubbing back returns it exactly. For placeholder rows the absolute phase is still art.

Elements are J2000 mean elements with secular node and periapsis rates and **no periodic
terms** (the Moon has them: see above). Frames: Triton's is genuinely ecliptic; the Galileans' and Titan's are
fits of Horizons ecliptic osculating elements, so their inclinations carry the parent's
obliquity (2.2° for Jupiter, 27.7° for Saturn) rather than being Laplace-plane values.

**Earth's rotation** is GMST at the epoch, not an accumulator, so a given UTC always gives the
same terminator and scrubbing backwards is exact. The texture's prime meridian is aligned by an
art constant that has not been calibrated against a reference image — the rate and the epoch
behaviour are the honest parts.

**Time scale.** Julian dates are treated as UTC (≈ UT1) throughout, which is exactly right for
Earth rotation (GMST) and good enough for the planets: UTC↔TT (~69 s) and UT1↔UTC (< 0.9 s) are
far below the arcminute the planet series provides. The Moon is the one body that needs it, and
gets `Ephemeris::DeltaTSeconds` applied (see "The Moon" above). Event times for the Moon and
Sun are good to ~10 s against NASA's 2017 circumstances; the planets' are good to minutes. Do
not read them as contact times for any other body.

**Umbra rendering** is a decal in the lighting pass, not a second shadow map.
`Application::ConfigureEclipseUmbra` picks at most one moon per planet — the one whose shadow
axis passes nearest the planet's centre — and uploads its scene-space centre and radius;
`planetLighting.fs` multiplies the shadow term by `EclipseVisibility()`, a sphere-occluder
falloff between the geometric umbra and penumbra radii. There is no extra FBO and no geometry
shader. The Low quality preset sets the star radius to zero, collapsing the falloff to a
hard-edged disc; Full uses the real cone plus an art softening factor, because the Sun is drawn
far smaller than life and the true penumbra at this scale is only a few scene units wide. The
caster is cleared before the moons draw, since they share `_mainPlanetShader` with their primary.

**Web surface.** `GetNextSkyEventJson` is the one structured export: a single JSON string rather
than a fan of `GetEclipseX/Y/Z` scalars, because an event's body pair only means something
alongside its kind tag. `web/src/skyEvents.ts` parses it and owns the chip; `web/src/
conjunction.ts` remains for the explorer's conjunction-only chip.

### 11.1 Precision and scale (how positions reach the GPU)

**World frame.** Simulation state is heliocentric scene units in double:
`Transformable::Place` / `GetWorldPosition`, `Camera::GetWorldPosition`,
`OrbitLayout::GetHeliocentricScenePosition`. Realistic scale is true scale — 1 AU = 1900
units, `SceneScale::kKmPerSceneUnit` ≈ 78 737 km — so Earth's radius is 0.081 units and a
metre is 1.27e-8 units, below a float's spacing anywhere more than ~0.1 units from the
origin. `OrbitLayout::GetOffset` (the float cast) is for coarse callers only.

**Compressed is a view of the same bodies.** The scale mode picks, per body, where it sits
and how big it is drawn (`Solar_System/SceneScale.h`). Compressed puts planets on the art
orbit radii, moons on their catalog `sceneOrbitRadius`, and draws every body at its art
radius; realistic draws the true radius (`radiusKm`) at the true distances.
`SpaceObject::SizeFactor()` is drawn size over art size (1 on compressed), and every art
length tied to a body — atmosphere and cloud shells, rings, focus distances, LOD distances,
magnetic-field radius, music ducking, Observe height — is multiplied by it, so those numbers
stay compressed-scale numbers. `Application::PlaceBodies` places every body from the date
each frame, so `ApplyOrbitScaleMode` switches without reloading; the camera keeps its offset
in radii from its nearest body (`CaptureCameraBodyFrame` / `RestoreCameraBodyFrame`).

**Floating origin.** The render origin (`OrbitLayout::GetRenderOrigin`, double) is put on
the camera once per frame in both modes, after everything that moves the camera
(`UpdateRenderOrigin` → `MoveRenderOrigin`). Every float position — model matrices
(`Transformable::Rebase`), uniforms, overlay VBOs — is world minus origin taken in double
(`ToRenderFrame`, `GetRenderPosition`); the camera sits at an exact zero and the view matrix
has no translation (`Camera::GetViewMatrix(eyeRender)`). `MoveRenderOrigin` carries what
holds a render-frame position across the shift: this frame's XR eye views (webxr.ts built
them before it), XR pointer rays, the bodies, staged-loading proxies and the mission-path
origin. Mission paths are uploaded relative to `_missionPathOrigin` and drawn with a
`pathOffset` uniform, re-uploading only once the camera is 512 units from it. On compressed
scale the moving origin changes positions by float rounding only: the golden poses differ
from the Sun-origin build by under 0.04 px, all of it the ulp of the camera position that
the old `lookAt(camPos, camPos + front)` lost from its forward axis.

**Depth.** One logarithmic curve for every pass (`common/log_depth.glsl`):
depth = log2(1 + C·w) / log2(1 + C·far), `frame.zCoef` = 2 / log2(1 + C·far),
`frame.logDepthC` = C. Compressed keeps near 0.001, far 20000 and C = 1, bit-identical to
the curve it always used. Realistic uses far 1e6, a near plane of half the altitude above
the nearest surface (0.25 m up to 0.001 units) and C = 1/near
(`CameraNavigation::ChooseClipPlanes`): a 24-bit buffer then separates 1 cm at 1 m and
1000 km at 1 AU (`test_mesh_anchor.cpp`). Reversed-Z was the alternative, but its precision
needs `glClipControl` (GL 4.5 / `EXT_clip_control`), which WebGL 2 does not dependably
have, so the web needed log depth regardless; one scheme serves both targets.

The vertex form (`ApplyLogDepth`) interpolates z linearly on screen, which is only right
while a triangle is small next to its distance; the facet under the camera is neither. With
the eye within 1.5× a body's bounding radius (`Renderer::NearSurface`), its surface, cloud
and atmosphere programs use the **`NEAR_SURFACE`** permutation:
- depth is written per fragment from an interpolated 1 + C·w (`PrepareLogFragDepth` /
  `LogFragDepth`; exact, as it is linear in w);
- clip z is parked at −`frame.nearPlane`, so clipping happens at the navigation's near
  plane whatever near the projection was built with (WebXR keeps a fixed depthNear). Not
  z = 0: that is the eye plane, and vertices clipped there divide by zero;
- vertices are placed relative to the eye (below).

The near programs compile on first use; until then a body draws with its normal program.

**Relative-to-eye vertices** (`gfx/MeshAnchor.h`). `model * aPos` adds a body-sized
translation and a body-sized vertex offset in float: 0.6 m of error on realistic Earth.
`NEAR_SURFACE` planetLighting.vs computes `mat3(model) * (aPos - meshAnchor) +
meshAnchorOffset`, where `meshAnchor` is the eye in model space rounded to float and
`meshAnchorOffset` the render-frame position of that rounded point, both from double: a
vertex under the eye lands within a millimetre.

**Surface patch** (`gfx/SurfacePatch.h`, `Renderer::DrawSurfacePatch`). sphere.obj's facets
are ~600 km across on realistic Earth. When one passes a metre under the eye, the GPU clips
it by interpolating corners hundreds of kilometres away in float, and the clipped edges land
about a centimetre off — 1–2 % of the screen at that range, seen as holes along the screen
borders (measured: 13 700 hole pixels at 0.5 m, 800 at 5 m, none at 50 m). The patch
re-tessellates every facet that comes within size/1000 of the eye until each piece is at
most 1000× its distance (or the near plane), with barycentric attributes and positions
stored as offsets from the eye (a float at the mesh radius is spaced 0.75 m), and draws it
over the mesh with the same program; the facet underneath fills any T-junction crack. It
exists only below ~600 m over Earth and is rebuilt when the eye moves, once per frame for
both XR eyes. Terrain tiles (#151 phase 2) will replace it where a body has a DEM. Shells
get no patch: an eye within a few hundred metres of a cloud or atmosphere shell (crossing
it) can see the same holes in that shell for the moment it takes.

**Navigation** (GL-free math in `core/CameraNavigation.h`, fed by `CameraController.cpp`).
- *Co-moving anchor:* the camera carries the motion of the body it is near (engage within
  64 radii, release past 80, hand over to a body twice as close), so a planet does not run
  off at 30 km/s and a date jump keeps it in view. A focus flight pins its target; mission
  follow and Observe pose the camera themselves and suspend the anchor. It carries
  translation, not spin: with time running, the ground slides under a hovering camera.
- *Speed:* on realistic scale movement is scaled by the altitude above the nearest surface,
  altitude per second at the default speed, from 1 mm/s to 1 AU/s. Compressed keeps its
  constant speed.
- *Floor:* the camera stays a margin above the drawn surface (0.5 m realistic, 0.002 units
  compressed). Within 0.05 radii the facets are ray-cast (`FacetRadius`, double
  Möller–Trumbore cast inward from outside the mesh, so an eye that ended up beneath a facet
  still finds it), since they sit up to kilometres inside the analytic sphere.

**Interfaces.** The camera pose JS reads and writes is heliocentric: `SetCameraPose` takes
doubles, and deep links keep 1e-9-unit coordinates on realistic scale (8 cm).
`GetCameraPositionX/Y/Z` stay render-frame, because webxr.ts composes eye views from them.
Native `SOLARSYSTEM_SCALE=compressed|realistic` runs the toggle at start-up, before
`SOLARSYSTEM_CAMERA_POSE`, so a pose can be given in realistic coordinates.

**Known limits.** No co-rotation (above). Shadow maps keep their ±3-radii light frustum, so
near the ground on realistic scale a shadow texel is kilometres wide.

**Tests.** `test_render_origin.cpp`: a flyby 90 AU out stays on its exact track to 0.01 px
with the origin on the camera, while the float-only control is over 10 px off; compressed
positions match the float computation they replaced. `test_camera_navigation.cpp`: altitude,
the floor from above and beneath a facet, clip planes, speed, anchor hysteresis, the camera
moving in double. `test_mesh_anchor.cpp`: millimetre placement and both depth curves.
`test_surface_patch.cpp`: the pieces tile their facet in place to the millimetre and meet
the size rule.

## 12. Common pitfalls

1. Do not use a blocking main loop on WASM.
2. Do not spawn `std::thread` in the web build.
3. Do not use geometry or compute shaders on web.
4. Route double uniforms through `Shader::Set*Double`, not `glUniform1d`.
5. Do not open the app via `file://` — always use HTTP.
6. Do not preload sounds in the Emscripten CMake block (Git LFS files may be missing).
7. After `TextureImage2D::ReloadTexture`, render passes must rebind texture IDs each frame.
8. Black textures on web usually mean incorrect `GL_TEXTURE_MAX_LEVEL` — verify mip chain upload.
9. Do not add a heliocentric float to a render-frame float (`sun->GetPosition() + GetOffset(...)`)
   or cache a render-frame position across frames without shifting it in `MoveRenderOrigin`;
   keep world positions (`GetWorldPosition`) in double instead, and multiply an art length
   tied to a body by its `SizeFactor()` (§11.1).

---

## 13. Local verification (summary)

```bash
./build-web.sh
cd web && npm run preview
# Open http://localhost:4173/solar-system/
```

In the browser console:

```js
window.setCameraPose(1850, 20, 30, 90, -10);  // approach Earth proxy
window.setQualityPreset(0);                     // 0=low, 1=medium, 2=full
```

Expect proxy labels → staged download → low-res planet → LOD streaming overlay when close. With placeholders only, visual quality change is minimal; console logs and UI prove the code paths.

Native, under Xvfb (`export MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460`):

```bash
cmake -B build-check -DSOLARSYSTEM_BUILD_TESTS=ON && cmake --build build-check -j
ctest --test-dir build-check --exclude-regex '^gl\.'          # GL-free unit tests
xvfb-run -a build-check/tests/SolarSystemGlTests              # GL tests (§7.2)
SOLARSYSTEM_SHADER_SELFTEST=1 xvfb-run -a build-check/SolarSystem   # every permutation, both dialects
SOLARSYSTEM_LOG=debug xvfb-run -a build-check/SolarSystem 2>&1 | grep FrameStats
scripts/check-gl-handles.sh && scripts/check-source-rules.sh
```

Full scenarios: [docs/plans/TESTING_GUIDE.md](plans/TESTING_GUIDE.md).

---

## 14. Related documentation

| Document | Purpose |
|----------|---------|
| [README.md](../README.md) | Build, features, asset hosting, deployment |
| [docs/plans/TESTING_GUIDE.md](plans/TESTING_GUIDE.md) | Step-by-step LOD/staged testing |
| [docs/plans/PORTING_GUIDE.md](plans/PORTING_GUIDE.md) | Emscripten port history and CMake flags |
| [docs/plans/WEBGPU_COMPANION_PLAN.md](plans/WEBGPU_COMPANION_PLAN.md) | Future Three.js/WebGPU companion (opt-in) |
| [docs/CROSS_ORIGIN_HEADERS.md](CROSS_ORIGIN_HEADERS.md) | COEP/CORS/CORP matrix, CDN vs app headers, local verification |
| [AGENTS.md](../AGENTS.md) | AI agent environment notes (Cursor Cloud VM) |
| [docs/plans/archive/](plans/archive/) | Completed implementation plans and PR notes |

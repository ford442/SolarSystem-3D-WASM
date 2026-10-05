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

**Graphics features:** atmospheric scattering, PCF/ray-traced shadows, cloud shadows, lens flare, HDR, normal mapping, planetary rings, instanced main-belt asteroid field + comet tails.

---

## 2. Repository layout

```
SolarSystem-3D-WASM/
├── src/                        # C++ application
│   ├── Application.h/.cpp      # Main loop, loading, rendering
│   ├── Auxiliary_Modules/      # Engine subsystems (Shader, Camera, LOD queue, …)
│   └── Solar_System/           # Celestial bodies by system (Earth_System/, …)
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
row count must stay in lockstep with `OrbitLayout::Body` / `kBodyCount` (Sun=0 … Vesta=11).

`src/Solar_System/BodyCatalog.generated.h` is generated from each body's `render` block
(shader flags, ambient factor, texture LOD ids, display names, `earthRadiusScale`,
`axialTiltDegrees`) for planets, dwarf planets, and moons. `CatalogBody` /
`CatalogSatellite` / `CatalogClouds` consume a row directly, so a rocky body whose
only distinguishing features fit in that row needs **no C++ class at all**. Mercury–Pluto,
Ceres, Vesta, the Moon, the Galileans, and the other currently rendered moons are built
this way. Adding another catalog-only rocky body is: catalog row + textures + `initTag`
on the allowlist (the factory loops catalog systems; `MakePlanetInitFunc` is generic).

**Axial tilt SSOT:** `orbit.axialTiltDegrees` is applied on both the WASM/C++ scene
(`CatalogBody::AdjustToParent` does `Rotate(degrees, Z)`) and the Three.js companion
(`orbital-parameters.json`). Optional `render.artTiltXDegrees` is a Saturn-only art
overlay so the rings keep their established presentation; it is *not* a second IAU
value. This replaced the previous split where C++ used `Rotate(81.2°, X)` for Uranus
vs catalog `−97.8°`, `+28.3°` vs `−28.3°` for Neptune, and no tilt at all for Pluto.

Atmospheres are catalog data too: `render.atmosphere` carries the O'Neil shell numbers
(`oneil`) and, for Earth/Venus/Mars/Titan, the physical coefficients (`physical`) that
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
| `Application` | Scene graph, render passes, loading orchestration, quality presets |
| `SystemModules.h` | Platform-conditional OpenGL/audio includes; `glBindTextureUnit` polyfill on WASM |
| `Shader` | GLSL compile/link; lazy uniform-location cache; `Set*Double` casts to float on WASM |
| `ShaderSource` | GL-free source assembly: `#version` rewrite, `common/preamble.glsl`, `#include`, `#line` bookkeeping (§9.1) |

### 7.2 Auxiliary

| Module | Role |
|--------|------|
| `Camera`, `FPS_Handler` | First-person navigation |
| `ShadowMapFBO` | PCF / ray-traced shadows |
| `HDR`, `LensFlare` | Post-processing (star glow only) |
| `FloatLutTexture` | Loads a baked RGBA16F LUT (atmosphere); quiet `nullopt` instead of a checkerboard |
| `MagneticFieldBloom` | Half-res field-line bloom (not the star HDR FBO) |
| `TextureImage2D` | DDS load, reload, mip level management |
| `WebResourceFetcher` | `DownloadFile` (async, callback-based) + `RequireResident` (residency probe) — WASM only |
| `TextureLoadingQueue` | Serialized high-res LOD downloads |
| `OrbitPathRenderer` | Faint heliocentric `GL_LINE_LOOP` guides |
| `MissionCatalog` / `MissionPathRenderer` | Sampled probe ribbons (Voyager); quality downsample; AU→scene via `HelioAuToScene` |
| `XrPointerRenderer` | WebXR controller rays (skipped on Low) |
| `Ephemeris` | `IEphemeris` backend: Standish planets, Keplerian Pluto/belt bodies, Keplerian moons, GMST |
| `SkyEvents` | Conjunction, eclipse, transit, and shadow-transit searches over the active backend |
| `MagneticFieldTracer` / `MagneticFieldLineRenderer` / `MagneticFieldBloom` | Optional dipole+toroidal ribbons (static VBO, GPU flow, half-res bloom on Medium+) |

### 7.3 Scene objects

Inheritance: `SpaceObject` → `Transformable` → `Planet` / `Satellite` / `Star`.

Each planet system lives in `src/Solar_System/<Name>_System/`. `SolarSystem.h` aggregates includes. Atmospheres, clouds, and rings are separate render components. `Atmosphere` is configured from its catalog row and owns its LUTs; `AtmosphereModel` is the GL-free physics behind them, shared with the baker and the tests (§9.2).

**Magnetic field params:** `MagneticFieldParams` on `SpaceObject` (set in `StarSystemFactory.cpp` via `MagneticFieldCatalog::IntrinsicParamsForBody`). Values are visual/educational — not SI magnetosphere physics. `ParamsForBody(body, quality)` adds seed/sample scaling and quality enable gates for the ribbon renderer. `Application::ForEachEnabledMagneticField` walks the Sun, loaded planets, and satellites whose `enabled` flag is set. Satellites default to disabled. Magnetic mode (`SetMagneticFields` / alias `SetMagneticFieldMode`, settings **M**) dims planet/cloud/atmosphere/ring shaders via `uSurfaceDim` and fades orbit paths; the Sun is left at full brightness.

---

## 8. Web frontend (`web/`)

| File | Role |
|------|------|
| `index.html` | Canvas, loading overlay, settings panel, streaming progress bar, tour caption, XR HUD |
| `src/main.ts` | Module init, `updateLoadingProgress`, `updateStreamingProgress`, `setCameraPose`, settings persistence |
| `src/tourPlayer.ts` | Guided-tour playlist over `SolarSystemRuntime` |
| `src/educationalLayer.ts` | Mission list + tour controls |
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
| Double uniforms | `glUniform1d` | Cast to float via `Shader` class |

**One source, two dialects.** Every file in `resource/shaders/` is written once in GLSL
ES 3.00, because that is the only dialect WebGL 2 accepts. The native build runs an
OpenGL 4.6 **core** context, which accepts `#version 300 es` only through
`ARB_ES3_compatibility` — widely implemented, but not guaranteed. So `ShaderSource`
(`src/Auxiliary_Modules/ShaderSource.cpp`, called by `Shader.cpp`) rewrites the leading
`#version 300 es` directive to `#version 460 core` on native builds only (GLSL ES 3.00 is a
subset of desktop GLSL 4.60, precision qualifiers included).

**Preamble and includes.** Every stage is assembled the same way on both targets:

```
#version …                         line 1 of the file (rewritten natively)
#define SS_STAGE_VERTEX|FRAGMENT 1, SS_GLSL_ES|SS_GLSL_DESKTOP 1
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
in both dialects (Mesa's core context also accepts `300 es`), prints
`[ShaderSelfTest] N/N … OK` and exits; the native CI job runs it under Xvfb with
`MESA_GLSL_VERSION_OVERRIDE=460`.

**No geometry stage on web.** WebGL 2 has no geometry shader, so the `Shader` constructor
overload taking a `geometryPath` is compiled out under `__EMSCRIPTEN__`: passing one in a
web build is a compile error, not a shader that silently fails to link.

**Material binding.** `planetLighting` material state for every catalog body goes through
`CatalogMaterial::BindMaterial` (`src/Solar_System/CatalogMaterial.h`). `CatalogBody` and
`CatalogSatellite` build a `CatalogMaterial::Material` from their row's `render.shaderFlags`,
`useSphereIntersect`, and `ambientFactor`. `BindMaterial` sets the full flag block on every
draw, so no body inherits the previous draw's `hasClouds`/`hasNightTexture`. It also binds
samplers to fixed units: diffuse 0, normal 1, specular 2, night 3, clouds 4. `SceneRenderer`
owns 6 (`shadowMap`) and 7 (`ringDiffuse`); the atmosphere passes use 9 (`ringDiffuse`),
11 (`shadowMap`, O'Neil) and 12/13 (transmittance / multi-scattering LUTs). `generate-planet-metadata.mjs` rejects a row
whose `shaderFlags` disagree with its `lod` ids, and a moon row that asks for night or cloud
maps. **A new rocky body needs only a catalog row, not a `Render()`.**

Special cases that stay outside the catalog material path:
- `Sun`: star, corona, and glow programs.
- `SaturnRing` / `UranusRing`: ring meshes on `planetaryRingLighting`.
- `CatalogClouds`: cloud shells on `cloudsLighting.fs`, with their own `cloudsNormalMap`
  binding.
- `Atmosphere`: the O'Neil and LUT programs, configured from `render.atmosphere` (§9.2).
- Asteroid field, comet tails, and the orbit/mission/magnetic-field overlays.

**Uniform locations:** `Shader::Set*` resolves each uniform name with `glGetUniformLocation` on first use and caches the `GLint` (including `-1` for missing names) in a per-program map. Subsequent sets reuse the cache — do not call `glGetUniformLocation` at render call sites. The cache is cleared when the program is deleted (`Release` / destructor). If a re-link path is added later, clear the cache after a successful `glLinkProgram`.

### 9.2 Atmospheres and the Sun

**Two atmosphere paths, one catalog row.** `render.atmosphere.oneil` drives the O'Neil
single-scattering shell (`atmosphere.fs`), which every preset can afford and which is the
only path for the gas giants and Pluto. Bodies whose row also has `physical`
(Earth, Venus, Mars, Titan) switch to `atmospherePbr.fs` on Medium/Full:

| Step | Where | What |
|------|-------|------|
| Bake (offline) | `tools/atmosphere_lut_baker` + `AtmosphereModel` | Transmittance LUT 256×64 (Bruneton 2017 mapping) and multi-scattering LUT 32×32 (Hillaire 2020), double precision → RGBA16F KTX2 with a parameter hash in the key/value data |
| Ship | `resource/atmosphere/<id>_{transmittance,multiscatter}.ktx2` | Preloaded into `.data` (~140 KB per body) |
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
| Nearest-planet search | `std::thread` | `UpdateSearchNearestPlanet()` every 60 frames |

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
placeholder angles on the old circular `SatelliteOrbit::Offset` path. See § Ephemeris accuracy. Keep focus indices **0–11 frozen** (Sun=0 … Vesta=11); new focus bodies continue at 12+
only after moons (moons already occupy 12+ as non-focus catalog rows).

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

**Moons.** A satellite is placed from its catalog `orbit.keplerian` row only when its index is
listed in `kKeplerianSatellites` (`Ephemeris.cpp`): today the Moon, Io, Europa, Ganymede,
Callisto, Titan, and Triton. Everything else still uses the circular `SatelliteOrbit::Offset` /
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
terms**. Frames: the Moon's and Triton's are genuinely ecliptic; the Galileans' and Titan's are
fits of Horizons ecliptic osculating elements, so their inclinations carry the parent's
obliquity (2.2° for Jupiter, 27.7° for Saturn) rather than being Laplace-plane values.

**Earth's rotation** is GMST at the epoch, not an accumulator, so a given UTC always gives the
same terminator and scrubbing backwards is exact. The texture's prime meridian is aligned by an
art constant that has not been calibrated against a reference image — the rate and the epoch
behaviour are the honest parts.

**Time scale.** Julian dates are treated as UTC throughout. UTC↔TT (~69 s, leap seconds and all)
and UT1↔UTC (< 0.9 s) are not modelled; both are far below the arcminute the planet series
provides. Do not read event times as contact times.

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

## 12. Common pitfalls

1. Do not use a blocking main loop on WASM.
2. Do not spawn `std::thread` in the web build.
3. Do not use geometry or compute shaders on web.
4. Route double uniforms through `Shader::Set*Double`, not `glUniform1d`.
5. Do not open the app via `file://` — always use HTTP.
6. Do not preload sounds in the Emscripten CMake block (Git LFS files may be missing).
7. After `TextureImage2D::ReloadTexture`, render passes must rebind texture IDs each frame.
8. Black textures on web usually mean incorrect `GL_TEXTURE_MAX_LEVEL` — verify mip chain upload.

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

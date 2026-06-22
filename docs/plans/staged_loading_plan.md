# Staged Planet Loading — Implementation Plan

## Goal
Replace the current monolithic upfront asset download with a **per-planet-system staged loader**. No planet textures or models are downloaded until the camera approaches within a configurable activation radius of that planet's orbital zone. Once assets arrive, the planet and its moons are initialized and enter the scene. The existing high-res LOD system then upgrades textures when the camera gets very close (~50 units).

---

## Current State Analysis

| Layer | Current Behavior (Web) | Problem |
|---|---|---|
| **Initial download** | `LoadResources()` downloads **all** low-res planet textures, **all** moon textures, models, skybox, sun textures, and sounds in one batch (~60+ files). | Massive upfront bandwidth and time. Many assets may never be visited in a session. |
| **Object creation** | Inner planets (Mercury–Mars) are created immediately in `InitStarSystem()`. Outer planets are deferred via `_deferredPlanetInits` but their textures are already in MEMFS. | Outer-planet deferral only saves GPU memory, not download time or initial CPU work. |
| **LOD upgrade** | `LoadHighResIfClose()` upgrades individual planets from low-res → high-res when camera < 50 units. Uses `TextureLoadingQueue` + `WebResourceFetcher::DownloadFile()`. | Works well; we keep this as Stage 2 of the new pipeline. |
| **Loading UI** | Single progress bar for the initial resource manifest. | Needs per-planet loading indicators once staged loading is active. |

**Key Insight:** The outer-planet deferred-init pattern (`_deferredPlanetInits`) is already ~50 % of what we need. We extend it to **all** planet systems and add **asset download gating** before `InitXxxSystem()` is allowed to run.

---

## Recommended Approach: Per-Planet-System Manifest Loader

### 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     RUNNING State                            │
│  ┌─────────────────┐                                        │
│  │ Core Assets     │  (skybox, sun, shaders, fonts, sounds) │
│  │ Loaded at boot  │                                        │
│  └─────────────────┘                                        │
│         │                                                   │
│         ▼                                                   │
│  ┌──────────────────────────────────────────────┐          │
│  │  PlanetSystemLoader::Update() — every frame  │          │
│  │   For each planet in NOT_LOADED state:        │          │
│  │     if cameraDist < activationRadius:         │          │
│  │       1. Transition → DOWNLOADING             │          │
│  │       2. Fire async downloads for manifest    │          │
│  │       3. On complete → call InitXxxSystem()   │          │
│  │       4. Transition → READY                   │          │
│  └──────────────────────────────────────────────┘          │
│         │                                                   │
│         ▼                                                   │
│  ┌──────────────────────────────────────────────┐          │
│  │  Existing LOD system (RenderPass)            │          │
│  │   if cameraDist < 50: LoadHighResIfClose()   │          │
│  │   (low-res → high-res texture upgrade)       │          │
│  └──────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────┘
```

### 2. Data Structures

**`PlanetSystemManifest`** — new struct in `Application.h` (or new `PlanetSystemLoader.h`):
```cpp
struct PlanetSystemManifest {
    std::string name;                       // e.g. "Earth_System"
    glm::vec3 proxyPosition;                // Orbital position for distance checks
    float activationRadius;                 // e.g. 1500.0f for outer, 800.0f for inner
    std::vector<std::string> assetPaths;    // Textures + models to download
    std::function<void()> initFunc;         // Calls InitEarthSystem(), etc.
    
    // Runtime state
    enum class State { NOT_LOADED, DOWNLOADING, READY } state = State::NOT_LOADED;
    std::atomic<int> pendingDownloads{0};
    int totalDownloads{0};
};
```

Replace `_deferredPlanetInits` with a single vector:
```cpp
std::vector<PlanetSystemManifest> _planetSystemManifests;
```

### 3. Asset Partitioning

**Core manifest** — downloaded during initial `LOADING` state (always needed):
- Models: `sphere.obj`, `phobos.obj`, `deimos.obj`, `saturn_ring.obj`, `uranus_ring.obj`
- SkyBox: 6 DDS faces
- Sun: `Star_Spectrum.dds`, `flares_bright.dds`
- Sounds: 5 MP3 tracks

**Per-planet manifests** — downloaded on-demand:
| System | Assets to download |
|---|---|
| Mercury | `Mercury_Diffuse_Low.dds`, `Mercury_Normal_Low.dds`, `Mercury_Specular_Low.dds` |
| Venus | `Venus_Diffuse_Low.dds`, `Venus_Normal_Low.dds` |
| Earth | `Earth_Day_Diffuse_Low.dds`, `Earth_Clouds_Diffuse.dds`, `Earth_Night_Diffuse.dds`, `Earth_Clouds_Normal.dds`, `Earth_Normal_Low.dds`, `Earth_Specular_Low.dds`, `Moon_Diffuse.dds`, `Moon_Normal.dds` |
| Mars | `Mars_Diffuse_Low.dds`, `Mars_Normal_Low.dds`, `Phobos_Diffuse.dds`, `Phobos_Normal.dds`, `Deimos_Diffuse.dds`, `Deimos_Normal.dds` |
| Jupiter | `Jupiter_Diffuse_Low.dds`, `Jupiter_Normal_Low.dds`, `Io_Diffuse.dds`, `Io_Normal.dds`, `Europa_Diffuse.dds`, `Europa_Normal.dds`, `Ganymede_Diffuse.dds`, `Ganymede_Normal.dds`, `Callisto_Diffuse.dds`, `Callisto_Normal.dds` |
| Saturn | `Saturn_Diffuse_Low.dds`, `Saturn_Normal_Low.dds`, `Saturn_Rings.dds`, `Mimas…Titan…Iapetus` diffuse+normal |
| Uranus | `Uranus_Diffuse_Low.dds`, `Uranus_Clouds_Diffuse.dds`, `Uranus_Normal_Low.dds`, `Uranus_Rings.dds`, `Uranus_Clouds_Normal.dds`, 5 moon textures |
| Neptune | `Neptune_Diffuse_Low.dds`, `Neptune_Clouds_Diffuse.dds`, `Neptune_Normal_Low.dds`, `Neptune_Clouds_Normal.dds`, `Triton_Diffuse.dds`, `Triton_Normal.dds` |
| Pluto | `Pluto_Diffuse_Low.dds`, `Pluto_Normal_Low.dds`, `Pluto_Specular_Low.dds`, `Charon_Diffuse.dds`, `Charon_Normal.dds`, `Charon_Specular.dds` |

> High-res textures are **not** in the manifest — they are fetched later by the existing `LoadHighResIfClose()` / `TextureLoadingQueue` pipeline.

### 4. State Machine per Planet System

```
NOT_LOADED ──[camera within radius]──► DOWNLOADING ──[all assets arrived]──► READY
```

- `NOT_LOADED`: Distance check runs every frame. Planet is invisible.
- `DOWNLOADING`: `WebResourceFetcher::DownloadFile()` called for each asset. A small on-screen label shows "Downloading Jupiter… 7/12". `_pendingDownloads` decremented in callbacks.
- `READY`: `initFunc()` is invoked (e.g. `InitJupiterSystem()`). The resulting `RenderableSceneComponent` is pushed into `_renderableSceneComponents` and rendered normally from then on.

### 5. Rendering While Loading

To avoid an empty solar system, we render **proxy orbital markers** for every `NOT_LOADED` or `DOWNLOADING` planet:
- A small glowing dot (billboard sprite or point) at `proxyPosition`
- Label with planet name and load status
- This gives the player a navigation target and assures them content exists

Implementation options:
1. **Reuse `TextRenderer`** with 3D positioning to draw a label like "Jupiter (downloading…)".
2. **Add a cheap billboard shader** that renders a colored dot for each manifest entry.

Option 1 is simpler and consistent with existing distance-label code. We can add a new `RenderPlanetProxyMarkers()` method in `Application`.

### 6. Integration with Existing Systems

| System | Change Required |
|---|---|
| **`Application::LoadResources()`** | Split into `LoadCoreResources()` and per-planet manifests. Remove all planet textures from the core list. |
| **`Application::InitStarSystem()`** | Do **not** call `InitMercury`, `InitVenus`, `InitEarthSystem`, `InitMarsSystem` immediately on web. Populate `_planetSystemManifests` for all 9 systems with their proxy positions (taken from the hard-coded `Translate()` values in each planet constructor). |
| **`Application::CheckAndInitDeferredPlanets()`** | Rename to `UpdatePlanetSystemLoading()`. Iterate `_planetSystemManifests`. Handle `NOT_LOADED` → `DOWNLOADING` → `READY` transitions. |
| **`Application::ProcessSceneComponentsRendering()`** | No change; empty `_renderableSceneComponents` is safe. |
| **`Application::RenderPlanetSatelliteStarDistances()`** | Add guard: skip if `_renderableSceneComponents.empty()`. |
| **`Application::_nearestPlanetIndex`** | Change type to `ssize_t` with `-1` sentinel when no planets loaded. All consumers must check `>= 0`. |
| **`CMakeLists.txt`** | Remove `--preload-file ${CMAKE_SOURCE_DIR}/resource/textures_low`. Only preload shaders, fonts, icons, and models. |
| **`TextureLoadingQueue`** | No change; continues to handle low-res → high-res upgrades. |
| **LOD (`LoadHighResIfClose`)** | No change; continues to work once planet is in `READY` state. |

### 7. Desktop Build Behavior

Native desktop should remain unchanged:
- `#ifdef __EMSCRIPTEN__` wraps the entire staged-loading infrastructure
- Desktop: `LoadResources()` loads everything at once (as today), `InitStarSystem()` creates all planets immediately

### 8. Memory & Performance Considerations

- **Initial download size**: Drops from ~60 assets to ~15 core assets (mostly tiny models + skybox + sounds). Estimated savings: 80–120 MB of initial download.
- **Per-planet download**: Largest is Saturn with ~18 textures; at low-res probably < 10 MB.
- **Proxy markers**: Negligible cost — a few 3D text labels rendered with existing `TextRenderer`.
- **GPU memory**: Only allocated when a planet is actually initialized.
- **Network**: Uses existing `emscripten_async_wget2` (concurrent, non-blocking). Multiple planet systems can download in parallel if the player is near several orbital zones.

### Related Issues
- #55 Staged loading (manifests, optionalAssetPaths, proxy markers with %, pending/total state)
- #53 Feedback (3D % labels + streaming overlay)
- #52 Async (DownloadFile for staged + queue for LOD)
- #58 Documentation alignment

### 9. File Modifications

| File | Action |
|---|---|
| `src/Application.h` | Replace `_deferredPlanetInits` with `_planetSystemManifests`. Add `UpdatePlanetSystemLoading()`, `RenderPlanetProxyMarkers()`, `LoadCoreResources()`. |
| `src/Application.cpp` | Re-implement `LoadResources()` / `LoadCoreResources()`. Update `InitStarSystem()` to populate manifests for all planets. Update `RunOneFrame()` to call new loader. Update `_nearestPlanetIndex` handling. |
| `src/Auxiliary_Modules/PlanetSystemLoader.h/.cpp` | *(Optional)* Extract loader logic into its own class if `Application.cpp` becomes too large. |
| `CMakeLists.txt` | Remove `textures_low` preload line. |
| `AGENTS.md` | Update section 9.5 to describe the new staged-loading architecture. |

### 10. Risk Mitigation

| Risk | Mitigation |
|---|---|
| Player reaches a planet before textures finish downloading | Activation radius (1500) is much larger than visual radius (~2–22). Player will see the proxy marker and label "Downloading…" for several seconds before the planet becomes visible. |
| `_nearestPlanetIndex` invalid (-1) | All consumers check `>= 0` before indexing `_renderableSceneComponents`. Star effects (lens flare, glow) that depend on nearest planet simply skip the ring-info optimization when no planets exist. |
| Sounds missing because they were removed from preload | Keep sounds in the core manifest (they are small MP3s and already handled by `Mix_LoadMUS`). The CMake comment already says sounds are excluded from preload due to LFS; we keep the runtime fetch for them. |
| High-res LOD tries to load before low-res is ready | `LoadHighResIfClose()` is only called in `RenderPass()`, which only runs for initialized `RenderableSceneComponent`s. A planet in `NOT_LOADED` or `DOWNLOADING` has no component, so LOD is naturally gated. |

---

## Alternative Approach (Not Recommended)

**"Keep Inner Planets Upfront, Only Defer Outer Planets' Assets"**
- Inner planets still download all textures at boot; only outer planets get deferred downloads.
- *Why rejected:* Does not meet the user's stated goal of "load planets one by one." It saves only ~40 % of initial bandwidth while keeping the monolithic download model for the most commonly visited planets (Earth/Mars).

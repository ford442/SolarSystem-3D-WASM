# CLAUDE.md

> **Canonical docs:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (system design) and [README.md](README.md) (build, assets, deploy). This file is Claude Code–specific quick reference; do not treat it as the sole architecture source.

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SolarSystem-3D-WASM is a dual-target 3D Solar System visualization:
- **Native**: C++17 with OpenGL 4.6 graphics (Windows native)
- **Web**: WebAssembly (via Emscripten) with WebGL 2, served with Vite + TypeScript

The project showcases advanced graphics techniques: atmospheric scattering, PCF shadows, ray-traced shadows, cloud layer shadows, lens flare, HDR rendering, and normal mapping.

## Building & Development

### Web Development

```bash
# From project root
cd web
npm install
npm run dev          # Start dev server at http://localhost:5173
npm run build        # Production build
```

The Vite dev server hot-reloads TypeScript changes. For C++ changes, rebuild the WASM module separately.

### Web Build (Emscripten)

```bash
# Full web build (CMake + Emscripten + Vite)
bash build-web.sh [--no-emsdk]

# Requires Emscripten SDK:
# source /path/to/emsdk/emsdk_env.sh
```

The build script:
1. Runs `setup_web_dependencies.sh` to prepare dependencies
2. Invokes `emcmake cmake` to configure for Emscripten
3. Builds with `emmake make`
4. Deploys artifacts: `SolarSystem.js` → `web/src/`, `SolarSystem.wasm` and `SolarSystem.data` → `web/public/`

The CMakeLists.txt has separate configurations for EMSCRIPTEN vs native builds, using Emscripten ports for GLFW, SDL2, SDL2_image, SDL_mixer, and FreeType.

### Native Build (Windows)

```bash
./build.sh
# Executable appears in build/ with required DLLs
```

## Code Architecture

### C++ Source Structure (`src/`)

**Core Modules**
- `main.cpp` — Application entry point
- `Application.h` — The `Application` class: lifecycle, window/input/audio, scene construction
  and the mission/magnetic-field-toggle public API. Its members are split across several
  translation units by responsibility; `Application.h` is the one place that declares them all:
  - `Application.cpp` — lifecycle (ctor/dtor, `InitScene`/`InitSceneObjects`) and the frame loop
  - `PlatformWindow.cpp` — GLFW/SDL/GL bring-up, window icon, resize, vsync, `Dispose`
  - `InputHandler.cpp` — polled input plus the mouse/scroll/key callbacks
  - `AudioPlayer.cpp` — background music (SDL_mixer on web, irrKlang on native)
  - `RenderSettings.cpp` — quality/shadow presets, orbit scale mode, texture LOD manager
  - `StarSystemFactory.cpp` / `PlanetSystemLoader.cpp` — scene construction and staged web loading
  - `XrSession.cpp` — WebXR eye state and the stereo render pass (web only)
- `Renderer.h` — The `Renderer` class: shadow/color/overlay/HDR draw passes and the GPU
  resources they own (shaders, shadow FBO, HDR, skybox, lens flare, text renderer, orbit/
  mission/magnetic-field overlay meshes). `Application` holds one (`_renderer`) and reaches it
  through public fields/methods; `Renderer` reaches back into scene state it doesn't own
  (camera, planet list, sun, mission/asteroid data) via a `friend`-granted `Application&`
  rather than duplicating storage — see the comment at the top of Renderer.h.
  - `SceneRenderer.cpp` — shadow/color passes for planets, atmospheres, rings, clouds, text overlays
  - `SceneOverlayRenderer.cpp` — magnetic field ribbons, orbit paths, mission paths, asteroid belt
- `SystemModules.h` — Convenience headers bundling system includes

**Auxiliary Modules** (`Auxiliary_Modules/`)
- Graphics: `Shader`, `Mesh`, `MeshHolder`, `TextureImage2D`
- Camera system: `Camera` with acceleration/zoom, `FPS_Handler`
- Rendering: `ShadowMapFBO` (PCF/ray-traced shadows), `LensFlare`, `HDR`, `TextRenderer`
- Async loading: `WebResourceFetcher` (Emscripten-specific async file loading via callback-based `emscripten_async_wget2`; the blocking `emscripten_wget_data` path is gone — see "No blocking fetches" below)

**Solar System** (`Solar_System/`)
- Base classes:
  - `SpaceObject.h/cpp` — Abstract base for all celestial bodies
  - `Transformable.h/cpp` — Rotation/translation transformations
  - `Planet.h/cpp`, `Satellite.h/cpp`, `Star.h/cpp` — Specializations
  - `CatalogBody` / `CatalogSatellite` / `CatalogClouds` — catalog-driven construction (Mercury–Pluto, moons, cloud shells)
  
- Specific celestial bodies that still have dedicated classes:
  - `Sun/Sun.h/cpp` — Star with corona and lens flare
  - `Saturn_System/SaturnRing`, `Uranus_System/UranusRing` — ring meshes
  - Atmosphere/ring *numbers* live in `SystemVisuals.h`; everything else is a catalog row
  
- Atmosphere/rings:
  - `Atmosphere.h/cpp` — Atmospheric scattering shader
  - `PlanetaryRing.h/cpp`, `SaturnRing.h/cpp`, `UranusRing.h/cpp` — Ring rendering
  - `Clouds.h/cpp`, `OuterShell.h/cpp` — Cloud layer and outer atmospheric shells
  
- Atmosphere/rings:
  - `Atmosphere.h/cpp` — Atmospheric scattering shader
  - `PlanetaryRing.h/cpp`, `SaturnRing.h/cpp`, `UranusRing.h/cpp` — Ring rendering
  - `Clouds.h/cpp`, `OuterShell.h/cpp` — Cloud layer and outer atmospheric shells
  
- Scene: `SkyBox.h/cpp`, `SolarSystem.h` — High-level scene management

**3rd Party** (`3rdparty/`)
- `nv_dds.h/cpp` — DDS texture format loader

### Web Frontend (`web/`)

- `index.html` — Canvas and loading UI
- `src/main.ts` — TypeScript entry point that:
  1. Imports the Emscripten-generated `SolarSystem.js` module
  2. Sets up canvas and loading progress bar
  3. Exposes `updateLoadingProgress()` for C++ to call
  4. Configures module locateFile() to resolve WASM/data files under Vite's base path
  5. Initializes the module with canvas and callbacks
- `src/style.css` — Loading screen and UI styling
- `vite.config.ts` — Vite configuration for module/ES6 export compatibility
- `tsconfig.json` — TypeScript configuration
- `package.json` — Dependencies (Vite, TypeScript)

### Key Integration Points

**Progress Tracking (C++ ↔ Web)**
- C++ calls `Module['updateLoadingProgress'](loaded, total)` from an `EM_ASM` block in `JsBridge.cpp` (same for `onSettingsChanged` / `updateStreamingProgress` / `onPlanetFocused`); `web/src/wasmCallbacks.ts` installs them on `Module`
- Bracket notation is load-bearing: Release links with `--closure 1`, which renames dotted property reads. A new bridge written as `Module.foo` is silently renamed and never fires in Release while working in Debug
- Web receives `WebResourceFetcher` events and updates progress bar

**Asset Resolution**
- Shaders, fonts, icons preloaded into Emscripten virtual filesystem via `--preload-file`
- Textures lazy-loaded at runtime via `WebResourceFetcher`
- Web sets `window.__solarSystemAssetBase` for runtime asset fetch base URL

**Memory Management**
- Emscripten initial memory: 256 MB; max 1 GB; `ALLOW_MEMORY_GROWTH=1`
- No `ASYNCIFY`/`JSPI`: downloads are callback-based, so nothing suspends the Wasm stack

## Key Architectural Decisions

- **Modular celestial bodies**: Each planet/satellite is its own class inheriting from base classes (Planet, Satellite, Star), making it easy to add new bodies or customize appearance.
- **Lazy texture loading**: Large DDS textures are not preloaded; `WebResourceFetcher` fetches them on-demand to avoid blocking initialization.
- **No blocking fetches**: every download goes through callback-based `WebResourceFetcher::DownloadFile` (`emscripten_async_wget2`), and C++ only ever reads files that are already resident in MEMFS. That keeps the build free of `ASYNCIFY`/`JSPI` and lets it use native `-fwasm-exceptions`. Anything that needs a new asset must stage it through `DownloadFile` (core resources, a planet manifest, or `TextureLoadingQueue`) before the code that reads it runs — see `docs/plans/PORTING_GUIDE.md` §3b.
- **Separate build paths**: CMakeLists.txt uses `if(EMSCRIPTEN)` to toggle library linking and compiler flags, avoiding duplication of core logic.
- **High memory ceiling**: 1 GB max allows the entire Solar System to load with high-resolution textures in modern browsers.

## Common Tasks

### Adding a New Celestial Body

**Catalog-driven path (preferred for a body with no unique shader needs).** No new class:
1. Add a body to `resource/planets.catalog.json` with a `render` block, `system.initTag` (also on `initTagAllowlist`), and `assets.requiredLow`
2. Run `node scripts/generate-planet-metadata.mjs`
3. Add the `OrbitLayout::Body` enum value and its `BodyFromName` mapping (focus indices 0–11 are frozen)
4. Add Keplerian elements to `Ephemeris.cpp` if the body is not in the Standish table
5. No factory edit: `InitStarSystem()` / `MakePlanetInitFunc` construct any catalog primary via `InitCatalogSystem`

Ceres, Vesta, Mercury–Pluto, the Moon, and the Galileans are the worked examples. Moons need `parent`, `orbit.keplerian` (elements plus the `OmegaDotDegPerDay` / `omegaDotDegPerDay` secular rates), and `orbit.sceneOrbitRadius`. A moon is only *placed* from those elements once its index is listed in `kKeplerianSatellites` in `Ephemeris.cpp`; until then it keeps the circular `SatelliteOrbit` offset, which is the right default while a row still carries placeholder node/periapsis angles. See docs/ARCHITECTURE.md § 11. `scripts/make_placeholder_dds.py` writes the stand-in textures that `resource/textures_low/` ships until real ones are uploaded.

**Hand-written path (Sun, ring meshes, or a shader that does not fit catalog flags).**
1. Keep or add a dedicated class (e.g., `Sun`, `SaturnRing`)
2. Register atmosphere/ring *numbers* in `src/Solar_System/SystemVisuals.h` rather than a new `Init*System`
3. Add textures/models to `resource/` and ensure they are accessible to the loader

### Modifying Shaders
- Shaders are in `resource/shaders/`
- Changes are reflected immediately in web (dev mode with Vite)
- For native builds, recompile after modifying shaders (they are embedded in the executable)

### Adjusting Graphics Settings (Shadows, Scattering, etc.)
- Most parameters are exposed in the UI (accessed via GUI)
- Fine-tuning uniforms: search for `glUniform*()` calls in rendering code
- Shadow map resolution: tune `ShadowMapFBO` resolution
- Memory constraints: may require adjusting texture resolution or preload lists for large texture sets

### Testing Web Locally
```bash
cd web
npm run dev
# Browser opens; changes to TypeScript auto-reload
# For C++ changes, rebuild via build-web.sh and refresh
```

## Debugging Notes

- **Emscripten console output**: Appears in browser DevTools Console
- **WebResourceFetcher errors**: Check Network tab for CORS or 404 errors
- **Memory issues**: If the app crashes during texture load, check Emscripten memory ceiling and Browser console for OOM messages
- **Performance**: Use WebGL profiling in DevTools; consider LOD (Level of Detail) for distant objects if needed
- **Progress bar stuck**: If `updateLoadingProgress()` is not called, verify `WebResourceFetcher` is invoking the callback and the window object is accessible from C++

## Dependencies & Third-Party

**C++ Libraries**
- GLFW, SDL2, SDL2_image — Window/input management (Emscripten ports provide these)
- Assimp — 3D model loading (pre-built WASM version in `external/assimp/build-wasm/`)
- GLM — Math library (header-only, in `external/glm/`)
- FreeType — Font rendering (Emscripten port)
- irrKlang — Audio (Windows native only; replaced with SDL_mixer on Emscripten)

**Web Libraries**
- Vite 7.2+ — Build tool and dev server
- TypeScript 5.9+ — Type-safe JavaScript

## Notes for Future Development

- [docs/plans/PORTING_GUIDE.md](docs/plans/PORTING_GUIDE.md) — Emscripten porting notes (memory, async, library substitutions)
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — staged loading, LOD, asset URL resolution
- Sound assets are managed via Git LFS and excluded from web preload to avoid CI/sandbox issues
- Textures are format-aware at runtime: `GlCapabilities` probes S3TC/BPTC/ETC2/ASTC, `TextureFormats::PreferredPackName()` picks a pack, and `TexturePaths::Resolve()` rewrites `.dds` paths to `<pack>/*.ktx2` when a deployment publishes one via `VITE_TEXTURE_PACKS`. With no pack published, a GPU without S3TC CPU-decodes the DXT blocks (`BlockCompression.cpp`) instead of showing the fallback checkerboard. No Basis/libktx transcoder is linked in — see docs/plans/PORTING_GUIDE.md § 3d
- Most DDS payloads are deploy artifacts; placeholders live in `resource/textures_low/`. Runtime fetches use `VITE_ASSET_BASE` or same-origin `resource/` (see README)
- Web deployment should use a server that correctly sets MIME types for `.wasm` files (application/wasm)

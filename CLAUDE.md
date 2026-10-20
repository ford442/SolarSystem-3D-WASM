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

The CMakeLists.txt has separate configurations for EMSCRIPTEN vs native builds, using Emscripten ports for GLFW, SDL2, SDL2_image, and FreeType (no SDL_mixer).

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
  - `CameraController.cpp` — per-frame camera navigation: co-moving anchor, altitude-scaled speed, the surface floor, clip planes (math in `core/CameraNavigation`)
  - `AudioPlayer.cpp` — native background music (SDL_mixer on Linux, irrKlang on Windows); web music is `web/src/music.ts`, C++ only reports `MusicDuckAmount()` via `onMusicDuck`
  - `RenderSettings.cpp` — quality/shadow presets, orbit scale mode, texture LOD manager
  - `StarSystemFactory.cpp` / `PlanetSystemLoader.cpp` — scene construction and staged web loading
  - `XrSession.cpp` — WebXR eye state and the stereo render pass (web only)
- `Renderer.h` — The `Renderer` class: shadow/color/overlay/HDR draw passes and the GPU
  resources they own (shaders, shadow FBO, HDR, skybox, lens flare, text renderer, orbit/
  mission/magnetic-field overlay meshes), all private. `Application` holds one (`_renderer`).
  Every pass takes a `FrameContext` (`FrameContext.h`: `CameraView`, `SceneSnapshot`,
  `SimState`, display size, XR flag), which `Application::BuildFrameSnapshot` fills after
  `UpdateScene`. The renderer has no reference back to `Application`, and there is no global
  `SimState`: anything a pass needs goes into the snapshot.
  - `FramePasses.h` — the GL-free frame order: simulate once, then per-component shadow pass followed by every view's colour pass (unit-tested). `Application::CullScene` drops components no view sees (bounding spheres vs `gfx/Frustum.h`) and orders the rest front to back; each light frustum's depth range is fitted to its component's bodies (ARCHITECTURE §7.5)
  - Scene objects hold no shader: a pass calls `SpaceObject::Draw(shader)` with the program it bound
  - `SceneRenderer.cpp` — shadow/color passes for planets, atmospheres, rings, clouds, text overlays
  - `SceneOverlayRenderer.cpp` — magnetic field ribbons, orbit paths, mission paths, asteroid belt
- `SystemModules.h` — Convenience headers bundling system includes (GL itself comes from `gfx/GlPlatform.h`)

**Subsystem folders** (ARCHITECTURE §7.2; there is no `Auxiliary_Modules/` any more)
- `core/` — `Log` (`LOG_*`), `Camera` (double world position) with acceleration/zoom, `CameraNavigation` (GL-free floor/speed/clip/anchor math), `FrameTimer`
- `gfx/` — every GL object: `GlHandle` (the only `glGen*`/`glDelete*`), `Shader`/`ShaderLibrary`/`ShaderSource`, `Mesh`/`MeshHolder`, `LineStripBatch`, `TextureImage2D`, `FloatLutTexture`, `ShadowMapFBO`, `HDR`, `FrameUniforms`, `TextureUnits.h`, `Frustum.h`, `GlCapabilities`
- `render/` — features built on `gfx/`: `LensFlare`, `TextRenderer`, orbit/mission/XR-pointer/magnetic-field renderers, field-line bloom
- `assets/` — `WebResourceFetcher` (callback-based `emscripten_async_wget2`; the blocking `emscripten_wget_data` path is gone — see "No blocking fetches" below), `TextureLoadingQueue`, `TextureLODController`, `PlanetManifestLoader`, `TextureFormatSupport`, `BlockCompression`
- `astro/` — GL-free: `Ephemeris`, `SkyEvents`, `Observer`, `LunarTheory`, `StarCatalog`, `MissionCatalog`, `MagneticField*` models, `SeatedView.h`
- Headers carry no `using namespace`, and `Renderer.h` forward-declares the GPU helpers it holds by `unique_ptr` — include their headers in the `.cpp` that uses them. `scripts/check-source-rules.sh` and `scripts/check-gl-handles.sh` (CI) enforce both rules plus "no `std::cout`/`std::cerr` in `src/`"

**Solar System** (`Solar_System/`)
- Base classes:
  - `SpaceObject.h/cpp` — Abstract base for all celestial bodies
  - `Transformable.h/cpp` — Rotation/translation transformations
  - `Planet.h/cpp`, `Satellite.h/cpp`, `Star.h/cpp` — Specializations
  - `CatalogBody` / `CatalogSatellite` / `CatalogClouds` — catalog-driven construction (Mercury–Pluto, moons, cloud shells)
  - `CatalogMaterial.h/cpp` — the one `planetLighting` material binder (flags from `render.shaderFlags`, fixed texture units)
  
- Specific celestial bodies that still have dedicated classes:
  - `Sun/Sun.h/cpp` — Star with corona and lens flare
  - `Saturn_System/SaturnRing`, `Uranus_System/UranusRing` — ring meshes
  - Atmosphere/ring *numbers* live in `SystemVisuals.h`; everything else is a catalog row
  
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
- C++ calls `Module['updateLoadingProgress'](loaded, total, failed)` from an `EM_ASM` block in `JsBridge.cpp` (same for `onSettingsChanged` / `updateStreamingProgress` / `onPlanetFocused` / `onNearestPlanetChanged` / `onMissionFocused`); `web/src/wasmCallbacks.ts` installs them on the module instance (`setModuleInstance`). The build is `MODULARIZE`, so there is no global `Module` — writing to `globalThis.Module` reaches nothing
- Bracket notation is load-bearing: Release links with `--closure 1`, which renames dotted property reads. A new bridge written as `Module.foo` is silently renamed and never fires in Release while working in Debug
- Web receives `WebResourceFetcher` events and updates progress bar

**Asset Resolution**
- Shaders, fonts, icons, and the baked atmosphere LUTs (`resource/atmosphere/`) preloaded into Emscripten virtual filesystem via `--preload-file`
- Textures lazy-loaded at runtime via `WebResourceFetcher`
- Web sets `window.__solarSystemAssetBase` for runtime asset fetch base URL

**Memory Management**
- Emscripten initial memory: 256 MB; max 1 GB; `ALLOW_MEMORY_GROWTH=1`
- No `ASYNCIFY`/`JSPI`: downloads are callback-based, so nothing suspends the Wasm stack

## Key Architectural Decisions

- **Catalog-driven celestial bodies**: planets and moons are `CatalogBody`/`CatalogSatellite` rows from `planets.catalog.json`, and all of them bind their material through `CatalogMaterial::BindMaterial`. The only dedicated classes left are for the Sun, the ring meshes, cloud shells, and atmospheres (list in ARCHITECTURE §9.1). Atmosphere numbers are catalog data too (`render.atmosphere`, ARCHITECTURE §9.2).
- **Baked atmosphere LUTs**: `render.atmosphere.physical` is baked by `tools/atmosphere_lut_baker` into `resource/atmosphere/*.ktx2`, which are committed. After editing it, rerun the generator and the baker; the `AtmosphereLutsFresh` ctest fails on a stale bake.
- **Precision and scale** (ARCHITECTURE §11.1): world positions are heliocentric doubles (`GetWorldPosition`, `Camera::GetWorldPosition`); the render origin sits on the camera every frame in both modes, so scene floats are world minus camera taken in double (`OrbitLayout::ToRenderFrame`) and the view matrix has no translation. Realistic scale is true scale (Earth 0.081 units); compressed is a per-body remap, and art lengths tied to a body multiply by `SpaceObject::SizeFactor()`. Don't keep a render-frame position across frames (shift it in `Application::MoveRenderOrigin`, or better store the world position). Close to a body its programs use the `NEAR_SURFACE` permutation (fragment log depth, relative-to-eye vertices, `gfx/SurfacePatch` under a camera metres up). Native `SOLARSYSTEM_SCALE=realistic` switches scale at start-up.
- **Poses are functions of the date**: planets (`OrbitLayout`), moons (`SatelliteOrbit::EphemerisOffset` / `MeanAnomalyAt`), and spins (`SpinDegreesAt`) derive from `OrbitLayout::GetJulianDate()`. Do not add per-frame accumulators — they break date-scrubber jumps.
- **Lazy texture loading**: Large DDS textures are not preloaded; `WebResourceFetcher` fetches them on-demand to avoid blocking initialization.
- **No blocking fetches**: every download goes through callback-based `WebResourceFetcher::DownloadFile` (`emscripten_async_wget2`), and C++ only ever reads files that are already resident in MEMFS. That keeps the build free of `ASYNCIFY`/`JSPI` and lets it use native `-fwasm-exceptions`. Anything that needs a new asset must stage it through `DownloadFile` (core resources, a planet manifest, or `TextureLoadingQueue`) before the code that reads it runs — see `docs/plans/PORTING_GUIDE.md` §3b.
- **Separate build paths**: CMakeLists.txt uses `if(EMSCRIPTEN)` to toggle library linking and compiler flags, avoiding duplication of core logic.
- **High memory ceiling**: 1 GB max allows the entire Solar System to load with high-resolution textures in modern browsers.

## Common Tasks

### Adding a New Celestial Body

**Catalog-driven path (preferred for a body with no unique shader needs).** No new class:
1. Add a body to `resource/planets.catalog.json` with a `render` block, `system.initTag` (also on `initTagAllowlist`), and `assets.requiredLow`
2. Run `node scripts/generate-planet-metadata.mjs`
3. Add the `OrbitLayout::Body` enum value and its `BodyFromName` mapping (focus indices 0–11 are frozen; Eris–Halley are 12–15, satellites follow at 16+)
4. Add Keplerian elements to `Ephemeris.cpp` if the body is not in the Standish table
5. No factory edit: `InitStarSystem()` / `MakePlanetInitFunc` construct any catalog primary via `InitCatalogSystem`

An atmosphere is a `render.atmosphere` row block, on a planet or a moon (Titan is the moon example): `oneil` for the cheap shell, plus `physical` for the LUT path — then build the `atmosphere_lut_baker` target (`-DSOLARSYSTEM_BUILD_TOOLS=ON`) and run `atmosphere_lut_baker --out resource/atmosphere` from the repo root (ARCHITECTURE §9.2). The giants are the cloud-deck example (ground = 1-bar equatorial radius). A flattened primary sets `render.oblateness`; `BodyShape.h` applies it to the surface, cloud and atmosphere meshes and gives the shaders their sphere-frame map — don't scale one mesh by hand.

Ceres, Vesta, Eris/Haumea/Makemake, 1P/Halley (kind `comet`), Mercury–Pluto, the Moon, and the Galileans are the worked examples. Moons need `parent`, `orbit.keplerian` (elements plus the `OmegaDotDegPerDay` / `omegaDotDegPerDay` secular rates), and `orbit.sceneOrbitRadius`. A moon is only *placed* from those elements once its index is listed in `kKeplerianSatellites` in `Ephemeris.cpp`; until then it keeps the circular `SatelliteOrbit` offset, which is the right default while a row still carries placeholder node/periapsis angles. See docs/ARCHITECTURE.md § 11. `scripts/make_placeholder_dds.py` writes the stand-in textures that `resource/textures_low/` ships until real ones are uploaded.

**Hand-written path (Sun, ring meshes, or a shader that does not fit catalog flags).**
1. Keep or add a dedicated class (e.g., `Sun`, `SaturnRing`)
2. Register ring *numbers* in `src/Solar_System/SystemVisuals.h` rather than a new `Init*System`
3. Add textures/models to `resource/` and ensure they are accessible to the loader

### Modifying Shaders
- Shaders are in `resource/shaders/`
- Every shader is authored once as `#version 300 es`; native `Shader.cpp` rewrites the directive to `#version 460 core` at load (ARCHITECTURE §9.1)
- Shared helpers live in `resource/shaders/common/` and are pulled in with `#include "common/x.glsl"` (expanded once per stage by `ShaderSource`, with `#line` so errors name the right file); `common/preamble.glsl` supplies the precision defaults. Don't paste a helper into a shader that can include it — `test_shader_source.cpp` fails on duplicate definitions
- Samplers name their unit: `layout(binding = TU_SHADOW_MAP) uniform sampler2D shadowMap;`. The `TU_*` names come from `src/gfx/TextureUnits.h` via the generated `common/texture_units.glsl` (`npm run generate:texture-units`, checked in CI). ShaderSource strips the qualifier and Shader sets the unit once at link, so C++ never `SetInt`s a sampler; bind with `glBindTextureUnit(Unit(TextureUnit::X), tex)`
- Depth is logarithmic everywhere: `ApplyLogDepth(gl_Position)` from `common/log_depth.glsl` in a vertex shader, or, under `NEAR_SURFACE`, `PrepareLogFragDepth` in the vertex stage and `gl_FragDepth = LogFragDepth(...)` in the fragment stage (ARCHITECTURE §11.1)
- Per-view values (view/proj matrices, eye and Sun position, near/far planes, zCoef, logDepthC, time) live in `FrameUBO` (`#include "common/frame.glsl"`, read as `frame.view` etc.), uploaded once per view by `Renderer::BeginView`. Don't add `uniform mat4 projection/view` or `zCoef` to a lit shader
- `Shader::Set*` takes a `string_view`, looks up a link-time location table, and skips the GL call when the value is unchanged. At debug level the `[FrameStats]` line logs pass counts and `glUniform*` calls every N frames (`SOLARSYSTEM_FRAME_STATS=N`, default 120)
- Per-material switches are `#define` permutations, not `uniform bool`s: a stage tests `#ifdef HAS_CLOUDS` and lists its switches on a `// permutations: …` line; programs are `ShaderSpec`s (files + defines) built once by `ShaderLibrary`. planetLighting's variant comes from `CatalogMaterial::Variant` per draw (ARCHITECTURE §9.1)
- `SOLARSYSTEM_SHADER_SELFTEST=1 ./build/SolarSystem` compiles every stage as both `460 core` and `300 es`, once per subset of its declared permutations, and exits (under Mesa/Xvfb also set `MESA_GLSL_VERSION_OVERRIDE=460`)
- Hot reload: native polls `resource/shaders/` mtimes every 0.5 s and rebuilds changed programs in place (`SOLARSYSTEM_SHADER_HOT_RELOAD=0` disables); under `npm run dev` call `await window.__solarSystem.reloadShaders()` in the console. A failed compile keeps the old program

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

- **Logging**: use `LOG_DEBUG/INFO/WARN/ERROR("Category", fmt, ...)` from `src/core/Log.h`, not `std::cout`/`std::cerr`; lines read `[Category] …` (tests grep for them). Level: `SOLARSYSTEM_LOG=debug` native, `?log=debug` web. Native Debug builds get a KHR_debug context whose messages arrive as `[GL] 0x…` (`SOLARSYSTEM_GL_DEBUG=1/0` overrides). See ARCHITECTURE §7.6
- **Emscripten console output**: Appears in browser DevTools Console (`console.debug` lines need the Verbose filter)
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
- irrKlang — Audio (Windows native only; SDL_mixer on Linux; web uses HTMLAudioElement)

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

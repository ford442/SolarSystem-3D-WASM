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
- `Application.h/cpp` — Main application class managing render loop and scenes
- `SystemModules.h` — Convenience headers bundling system includes

**Auxiliary Modules** (`Auxiliary_Modules/`)
- Graphics: `Shader`, `Mesh`, `MeshHolder`, `TextureImage2D`
- Camera system: `Camera` with acceleration/zoom, `FPS_Handler`
- Rendering: `ShadowMapFBO` (PCF/ray-traced shadows), `LensFlare`, `HDR`, `TextRenderer`
- Async loading: `WebResourceFetcher` (Emscripten-specific async file loading via `emscripten_wget_data`)

**Solar System** (`Solar_System/`)
- Base classes:
  - `SpaceObject.h/cpp` — Abstract base for all celestial bodies
  - `Transformable.h/cpp` — Rotation/translation transformations
  - `Planet.h/cpp`, `Satellite.h/cpp`, `Star.h/cpp` — Specializations
  
- Specific celestial bodies organized by system:
  - `Sun/Sun.h/cpp` — Star with corona and lens flare
  - `Earth_System/` — Earth, Moon, EarthClouds
  - `Mars_System/` — Mars, Phobos, Deimos
  - `Jupiter_System/` — Jupiter, Io, Europa, Ganymede, Callisto
  - `Saturn_System/` — Saturn, SaturnRing, Mimas, Enceladus, Tethys, Dione, Rhea, Titan, Iapetus
  - `Uranus_System/` — Uranus, UranusRing, Ariel, Miranda, Umbriel, Titania, Oberon, UranusClouds
  - `Neptune_System/` — Neptune, NeptuneClouds, Triton
  - `Pluto_System/` — Pluto, Charon
  
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
- C++ calls `emscripten_run_script()` to invoke `window.updateLoadingProgress(loaded, total)`
- Web receives `WebResourceFetcher` events and updates progress bar

**Asset Resolution**
- Shaders, fonts, icons preloaded into Emscripten virtual filesystem via `--preload-file`
- Textures lazy-loaded at runtime via `WebResourceFetcher`
- Web sets `window.__solarSystemAssetBase` for runtime asset fetch base URL

**Memory Management**
- Emscripten initial memory: 256 MB; max 1 GB; `ALLOW_MEMORY_GROWTH=1`
- `ASYNCIFY=1` enables async fetch without blocking render loop

## Key Architectural Decisions

- **Modular celestial bodies**: Each planet/satellite is its own class inheriting from base classes (Planet, Satellite, Star), making it easy to add new bodies or customize appearance.
- **Lazy texture loading**: Large DDS textures are not preloaded; `WebResourceFetcher` fetches them on-demand to avoid blocking initialization.
- **Emscripten async support**: `ASYNCIFY` allows `emscripten_wget_data()` calls without explicit async wrappers, simplifying C++ code.
- **Separate build paths**: CMakeLists.txt uses `if(EMSCRIPTEN)` to toggle library linking and compiler flags, avoiding duplication of core logic.
- **High memory ceiling**: 1 GB max allows the entire Solar System to load with high-resolution textures in modern browsers.

## Common Tasks

### Adding a New Celestial Body
1. Create a new class in the appropriate system folder (e.g., `Saturn_System/NewSatellite.h/cpp`)
2. Inherit from `Satellite` or `Planet` as appropriate
3. Override `update()` and `render()` if needed; use parent implementations for default behavior
4. Register the body in `SolarSystem.h` constructor or initialization
5. Add textures/models to `resource/` and ensure they are accessible to the loader

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
- Most DDS payloads are deploy artifacts; placeholders live in `resource/textures_low/`. Runtime fetches use `VITE_ASSET_BASE` or same-origin `resource/` (see README)
- Web deployment should use a server that correctly sets MIME types for `.wasm` files (application/wasm)

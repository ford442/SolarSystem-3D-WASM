<!-- AGENTS.md — Context for AI coding agents working on SolarSystem-3D-WASM -->

# SolarSystem-3D-WASM — Agent Context

## 1. Project Overview

**SolarSystem-3D-WASM** is a hybrid C++17 application that renders an animated, high-fidelity 3D Solar System. It targets two platforms from a single codebase:

- **Native Desktop** (Windows/Linux): OpenGL 4.6 Core Profile, GLFW windowing, irrKlang audio, full multi-threading.
- **Web Browser** (WebAssembly): WebGL 2 / OpenGL ES 3.0 via Emscripten, SDL_mixer audio, frame-based background tasks, async resource loading.

**Core Goal:** Maintain 100% feature parity between the native desktop build and the web build while respecting browser limitations (single-threaded loops, WebGL restrictions, memory constraints).

**Notable Features:**
- First-person camera with acceleration and zoom
- High-resolution DDS textures (8K+) with a runtime LOD system on web
- Blinn–Phong reflection with normal maps
- Atmospheric scattering, Mie scattering, cloud shadows
- PCF soft shadows via unidirectional shadow maps
- Lens flare and HDR post-processing
- On-screen text hints (FreeType) and 3D distance labels
- Background music with shuffle playback

---

## 2. Technology Stack

| Component | Native (Desktop) | Web (WASM) |
|-----------|------------------|------------|
| **Language** | C++17 | C++17 (compiled via Emscripten) |
| **Graphics API** | OpenGL 4.6 Core | WebGL 2 (OpenGL ES 3.0) |
| **Windowing** | GLFW3 | GLFW3 (Emscripten port `-s USE_GLFW=3`) |
| **Extension Loader** | GLEW | *None* (provided by Emscripten) |
| **Math** | GLM (header-only) | GLM (header-only) |
| **Model Loading** | Assimp (linked) | Assimp (static `libassimp.a`) |
| **Text Rendering** | FreeType (linked) | FreeType (Emscripten port `-s USE_FREETYPE=1`) |
| **Image I/O** | SDL2_image (linked) | SDL2_image (Emscripten port `-s USE_SDL_IMAGE=2`) |
| **Audio** | irrKlang (`ISoundEngine`) | SDL_mixer (`Mix_Music`) |
| **Build System** | CMake + Ninja | CMake + `emcmake` / `emmake` |
| **Web Frontend** | *N/A* | Vite + TypeScript |
| **Texture Format** | DDS (compressed, via `nv_dds`) | DDS (compressed, via `nv_dds`) |

---

## 3. Directory Structure

```
SolarSystem-3D-WASM/
├── CMakeLists.txt              # Root CMake; contains Emscripten conditional block
├── build.sh                    # Native desktop build script (CMake + Ninja)
├── build-web.sh                # Full WASM build script (deps → cmake → make → deploy)
├── setup_web_dependencies.sh   # Fetches GLM and builds libassimp.a for WASM
├── git.sh                      # Convenience: git add / commit / push
│
├── src/                        # All C++ source code
│   ├── main.cpp                # Entry point; calls Application::Exec()
│   ├── Application.h/.cpp      # Core app class: init, main loop, rendering, scene setup
│   ├── SystemModules.h         # Platform-conditional OpenGL/audio/FT includes
│   ├── 3rdparty/               # nv_dds (DDS texture loader)
│   ├── Auxiliary_Modules/      # Reusable engine subsystems
│   │   ├── Camera.h/.cpp
│   │   ├── Shader.h/.cpp
│   │   ├── Mesh.h/.cpp / MeshHolder.h/.cpp
│   │   ├── TextureImage2D.h/.cpp
│   │   ├── TextRenderer.h/.cpp
│   │   ├── FPS_Handler.h/.cpp
│   │   ├── ShadowMapFBO.h/.cpp
│   │   ├── HDR.h/.cpp
│   │   ├── LensFlare.h/.cpp
│   │   └── WebResourceFetcher.h/.cpp   # Async HTTP fetcher (WASM only)
│   └── Solar_System/           # Scene objects and planet-specific logic
│       ├── SpaceObject.h/.cpp / Transformable.h/.cpp
│       ├── Planet.h/.cpp / Satellite.h/.cpp / Star.h/.cpp
│       ├── Atmosphere.h/.cpp / Clouds.h/.cpp / PlanetaryRing.h/.cpp
│       ├── SkyBox.h/.cpp / OuterShell.h/.cpp
│       ├── SolarSystem.h       # PCH-style aggregate header for all planet classes
│       ├── Sun/
│       ├── Mercury/
│       ├── Venus/
│       ├── Earth_System/       # Earth, Moon, EarthClouds
│       ├── Mars_System/        # Mars, Phobos, Deimos
│       ├── Jupiter_System/     # Jupiter, Io, Europa, Ganymede, Callisto
│       ├── Saturn_System/      # Saturn, Mimas, Enceladus, Tethys, Dione, Rhea, Titan, Iapetus, SaturnRing
│       ├── Uranus_System/      # Uranus, Miranda, Ariel, Umbriel, Titania, Oberon, UranusRing, UranusClouds
│       ├── Neptune_System/     # Neptune, Triton, NeptuneClouds
│       └── Pluto_System/       # Pluto, Charon
│
├── resource/                   # Runtime assets (preloaded into VFS for WASM)
│   ├── shaders/                # GLSL vertex & fragment shaders
│   ├── textures/               # High-resolution DDS textures
│   ├── textures_low/           # Low-resolution DDS textures (WASM initial load)
│   ├── models/                 # OBJ models (sphere, phobos, deimos, rings)
│   ├── fonts/                  # TrueType fonts (Arial)
│   ├── icons/                  # Window icons
│   └── sounds/                 # MP3 background music (excluded from repo LFS)
│
├── web/                        # Web frontend (Vite project)
│   ├── index.html              # Entry HTML with loading progress bar overlay
│   ├── vite.config.ts          # Vite config; base path = /solar-system/
│   ├── tsconfig.json           # Strict TypeScript, ES2022, DOM types
│   ├── package.json            # Dev scripts: dev, build, build:emcc, preview
│   ├── src/
│   │   ├── main.ts             # TS entry: imports WASM module, wires progress bar
│   │   ├── SolarSystem.js      # **Generated** Emscripten glue code (copied by build-web.sh)
│   │   ├── SolarSystem.d.ts    # Type declarations for glue code
│   │   ├── style.css
│   │   └── counter.ts
│   ├── public/                 # Static assets served at root
│   │   ├── SolarSystem.wasm    # **Generated** WASM binary
│   │   └── SolarSystem.data    # **Generated** preloaded asset bundle
│   └── deploy.py               # SFTP deployment script (paramiko)
│
├── external/                   # Fetched by setup_web_dependencies.sh
│   ├── glm/                    # Git clone of g-truc/glm
│   └── assimp/                 # Git clone of assimp/assimp (v5.3.1)
│       └── build-wasm/         # Emscripten static build output
│
├── build/                      # Native CMake/Ninja build output
├── build-web/                  # Emscripten CMake/Make build output
└── doc/                        # Screenshots, project logo
```

---

## 4. Build, Test & Run Commands

### 4.1 Native Desktop Build

```bash
# From project root
./build.sh
```

This runs:
```bash
mkdir build && cd build
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

**Output:** `build/SolarSystem.exe` (Windows) or `build/SolarSystem` (Linux) plus copied DLLs.

**Run:**
```bash
cd build
./SolarSystem
```

### 4.2 WebAssembly Build

**Prerequisites:**
- Emscripten SDK installed and activated (`source /path/to/emsdk/emsdk_env.sh`)

```bash
# One-shot build (deps + cmake + make + deploy)
./build-web.sh

# Skip emsdk sourcing if already in PATH
./build-web.sh --no-emsdk
```

**What the script does:**
1. Sources `emsdk_env.sh` (unless `--no-emsdk`)
2. Runs `setup_web_dependencies.sh` → fetches GLM, builds `external/assimp/build-wasm/lib/libassimp.a`
3. Runs `emcmake cmake -B build-web .`
4. Runs `emmake make -j55` in `build-web/`
5. Copies artifacts:
   - `build-web/SolarSystem.js` → `web/src/SolarSystem.js`
   - `build-web/SolarSystem.wasm` → `web/public/SolarSystem.wasm`
   - `build-web/SolarSystem.data` → `web/public/SolarSystem.data`

**Serve the raw Emscripten output:**
```bash
cd build-web
python3 -m http.server 8000
# Open http://localhost:8000/SolarSystem.html
```

**Build and serve via Vite frontend:**
```bash
cd web
npm install          # if needed
npm run build        # Runs build:emcc + tsc + vite build
npm run preview      # Serves the Vite production build
```

> **Critical:** Do NOT open HTML files via `file://` protocol. CORS will block asset loading. Always use a local HTTP server.

### 4.3 Dependency Setup (Standalone)

```bash
./setup_web_dependencies.sh
```

Fetches `glm` and `assimp`, then builds a WASM-optimized static Assimp library with only OBJ, FBX, glTF, and Collada importers enabled.

---

## 5. Code Style & Conventions

### 5.1 Naming
- **Classes / Structs:** `PascalCase` (`Application`, `RenderableSceneComponent`, `PlanetInfo`)
- **Member variables:** Leading underscore + `camelCase` (`_mainWindow`, `_fpsHandler`, `_renderableSceneComponents`)
- **Local variables / parameters:** `camelCase` (`sphereModel`, `deltaTime`)
- **Functions / methods:** `PascalCase` (`InitSystems`, `RunOneFrame`, `RenderPass`)
- **Files:** Match the primary class name (`Application.cpp`, `ShadowMapFBO.h`)
- **Namespaces:** Not heavily used; anonymous namespaces are used for file-local globals (e.g., `camera`, `deltaTime` in `Application.h`)

### 5.2 Language & Comments
- The codebase is **English** with occasional **Russian** comments (e.g., in `Shader.cpp`).
- Prefer **English** for all new comments and log messages.
- Error messages are typically uppercase with `ERROR::SHADER::...` style prefixes in `Shader.cpp`.

### 5.3 Includes
- Project headers use `"quotes"`.
- System/3rd-party headers use `<angle brackets>`.
- `SystemModules.h` is the canonical place for platform-conditional OpenGL, audio, and math includes.

### 5.4 Smart Pointers
- Prefer `std::unique_ptr` for owned single objects.
- Prefer `std::shared_ptr` for planets and satellites that may be referenced by multiple systems (e.g., atmosphere parent pointers).
- `std::make_unique` / `std::make_shared` are used consistently.

### 5.5 Platform Branches
- The golden rule is `#ifdef __EMSCRIPTEN__` for web-specific logic, `#else` for native.
- Never invert the logic (`#ifndef __EMSCRIPTEN__` as the primary branch is discouraged).

---

## 6. Platform Conditionals & The Main Loop

### 6.1 The Golden Rule

Use `#ifdef __EMSCRIPTEN__` to separate platform logic.

- **Native:** Uses `irrKlang` for audio, `std::thread` for background tasks, full OpenGL 4.6, GLEW.
- **Web:** Uses `SDL_mixer` for audio, frame-based slicing for background tasks, OpenGL ES 3.0.

### 6.2 Main Loop (Critical)

**NEVER** use a blocking `while (!WindowShouldClose)` loop for the web build. It will freeze the browser.

**Pattern:** Logic must be encapsulated in `Application::RunOneFrame()`.

**Implementation:**

```cpp
// Application::Exec()
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

`RunOneFrame()` handles both the `LOADING` state (async resource downloads with progress bar) and the `RUNNING` state (full scene rendering).

### 6.3 Threading Refactoring

**Native:**
- `_backgroundMusicThread` — continuous music volume fading and track switching.
- `_searchNearestPlanetThread` — continuous nearest-planet calculation.

**Web:**
- `UpdateBackgroundMusic()` — called every frame; manages `Mix_Music` playback and timed track advances.
- `UpdateSearchNearestPlanet()` — called every frame; runs the search every 60 frames (~1 second at 60 FPS).

---

## 7. Shader Compatibility

| Aspect | Native | Web |
|--------|--------|-----|
| **Version Directive** | `#version 460 core` | `#version 300 es` |
| **Fragment Precision** | *Not required* | `precision highp float;` (and `highp int` where needed) |
| **In/Out Qualifiers** | `in` / `out` | `in` / `out` (same as GL 4.x) |
| **Double Precision** | `double` uniforms supported | **Not supported** — see below |

### 7.1 Double-Precision Uniform Handling

WebGL 2 does NOT support `glUniform1d` or `glUniform*dv`. The `Shader` class handles this transparently:

```cpp
void Shader::SetDouble(const std::string &name, double value) const {
    #ifdef __EMSCRIPTEN__
    glUniform1f(glGetUniformLocation(_shaderProgramID, name.c_str()), static_cast<float>(value));
    #else
    glUniform1d(glGetUniformLocation(_shaderProgramID, name.c_str()), value);
    #endif
}
```

All `Set*Double` methods follow the same pattern. Native builds preserve double precision; web builds cast to `float`.

### 7.2 Geometry & Compute Shaders

**Do not use Geometry or Compute Shaders.** They are not supported in WebGL 2 (ES 3.0). The `Shader` constructor accepts an optional geometry path, but on web builds geometry shaders will fail to compile.

### 7.3 Direct State Access (DSA) Polyfill

WebGL 2 lacks OpenGL 4.5+ DSA. `SystemModules.h` provides a polyfill for the most common case:

```cpp
#ifdef __EMSCRIPTEN__
inline void glBindTextureUnit(GLuint unit, GLuint texture) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}
#endif
```

> Cube-map texture unit binding may need manual handling if the polyfill is insufficient.

---

## 8. Audio Systems

| Feature | Native (`irrKlang`) | Web (`SDL_mixer`) |
|---------|---------------------|-------------------|
| **Init** | `createIrrKlangDevice(...)` | `Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048)` |
| **Playback** | `ISoundEngine::play2D()` | `Mix_PlayMusic(_currentMusic, 1)` |
| **Volume** | `_soundEngine->setSoundVolume(x)` | `Mix_VolumeMusic(int)` |
| **Threading** | Background thread with fade logic | Frame-based `UpdateBackgroundMusic()` |

**Important:** Do not attempt to include `irrKlang` headers in the web build path; they are excluded from include directories when `EMSCRIPTEN` is defined.

---

## 9. Memory, Assets & LOD Strategy

### 9.1 Virtual File System (WASM)

Assets in `resource/` are preloaded into Emscripten's virtual file system (MEMFS) using `--preload-file`. On the web, standard `std::ifstream` or `fopen` works transparently; relative paths resolve as if local.

**CMake preload directives (web only):**
```cmake
set(CMAKE_EXE_LINKER_FLAGS "... --preload-file ${CMAKE_SOURCE_DIR}/resource/shaders@/resource/shaders")
set(CMAKE_EXE_LINKER_FLAGS "... --preload-file ${CMAKE_SOURCE_DIR}/resource/fonts@/resource/fonts")
set(CMAKE_EXE_LINKER_FLAGS "... --preload-file ${CMAKE_SOURCE_DIR}/resource/icons@/resource/icons")
# Sounds are EXCLUDED from preload because they are stored via Git LFS and may be missing.
```

### 9.2 Async Resource Fetching (Web)

`WebResourceFetcher` provides two mechanisms:

1. **`DownloadFile(url, virtualPath, callback)`** — Uses `emscripten_async_wget2` for concurrent, non-blocking downloads with success/failure callbacks. Used during the `LOADING` state for the initial resource manifest.
2. **`Fetch(path)`** — Uses synchronous `emscripten_wget_data` for on-demand fetches (e.g., LOD high-res texture upgrades). Checks if the file already exists in MEMFS before fetching.

### 9.3 Loading Progress Bar

- C++ tracks `_resourcesPending` (atomic int) and `_totalResources`.
- `UpdateLoadingProgress()` calls into JavaScript via `EM_ASM`:
  ```cpp
  EM_ASM({
      if (typeof window.updateLoadingProgress === 'function') {
          window.updateLoadingProgress($0, $1);
      }
  }, loaded, _totalResources);
  ```
- `web/src/main.ts` exposes `window.updateLoadingProgress`, which updates the HTML/CSS progress bar and hides the loading overlay at 100%.

### 9.4 LOD (Level of Detail) Texture System

**Goal:** Reduce initial download size on web by loading low-res textures first, then upgrading to high-res when the camera is close.

- `GetTexturePath(lowRes, highRes)` returns `lowRes` for WASM and `highRes` for desktop.
- Low-res textures live in `resource/textures_low/`.
- Each frame, `RenderPass()` calls `planet->LoadHighResIfClose(camera.GetPosition())`.
- Threshold is typically **50 units** (configurable per planet).
- Once `_isHighResLoaded` is true, no further fetches occur for that planet.

**Implemented for:** Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune, Pluto.

### 9.5 Staged Planet Loading (Web Only)

To minimize initial download size and WASM memory usage, **no planet systems are initialized at startup**. Instead, each planet system has a `PlanetSystemManifest` that stores:
- A proxy orbital position for distance checks
- An activation radius (typically 800 for inner planets, 1500 for outer planets)
- A list of assets (textures, models) to download before initialization
- An `initFunc` callback that calls the existing `InitXxxSystem()` method

Each frame, `UpdatePlanetSystemLoading()` measures camera distance against all `NOT_LOADED` manifests. When the camera enters the activation radius:
1. The manifest transitions to `DOWNLOADING`
2. `WebResourceFetcher::DownloadFile()` fires async downloads for all assets in the manifest
3. Once all downloads complete, `initFunc()` is invoked, creating the planet and its moons
4. The manifest transitions to `READY`

While a planet is `NOT_LOADED` or `DOWNLOADING`, a 3D proxy marker is rendered at its proxy position using `RenderPlanetProxyMarkers()`, showing the planet name and load status.

**Desktop:** All planets are initialized immediately, as before.

---

## 10. Web Frontend Architecture

The `web/` directory is a standard Vite project.

- **Build tool:** Vite (dev server + bundler)
- **Language:** TypeScript (strict mode, `noUnusedLocals`, `noUnusedParameters`)
- **Entry:** `index.html` → `src/main.ts`
- **Base path:** `/solar-system/` (configurable in `vite.config.ts`)

**`main.ts` responsibilities:**
1. Select the `<canvas>` element for Emscripten.
2. Wire up the loading progress bar DOM elements.
3. Expose `window.updateLoadingProgress` for C++.
4. Expose `window.__solarSystemAssetBase` so `WebResourceFetcher` can resolve runtime asset URLs relative to the deployed base path.
5. Import the generated `SolarSystem.js` glue code as an ES module.
6. Instantiate the Module with `canvas`, `locateFile`, `print`, `printErr`, and `onRuntimeInitialized` callbacks.

---

## 11. Testing Strategy

This project does **not** currently have automated unit tests. Testing is manual and environment-specific.

### 11.1 Native Desktop Testing

```bash
./build.sh
cd build
./SolarSystem
```

**Expected:**
- Fullscreen window opens on primary monitor.
- High-res textures load immediately.
- No LOD messages appear (system is disabled on desktop).
- Background music starts shuffled.
- Camera controls (WASD, mouse look, scroll zoom) work.

### 11.2 WebAssembly Testing

```bash
./build-web.sh
cd build-web
python3 -m http.server 8000
```

Navigate to `http://localhost:8000/SolarSystem.html`.

**Checklist:**
1. Loading screen appears at 0%.
2. Progress bar advances as resources download.
3. Console shows `Loading 63 resources...` and per-resource success messages.
4. At 100%, loading screen hides and scene renders.
5. Earth/Mercury/etc. render with low-res textures initially.
6. Zooming to < 50 units triggers `[LOD] ... Loading high-res textures...` messages.
7. Visual quality improves after high-res fetch.
8. No CORS errors (verify you used `http://`, not `file://`).

### 11.3 Quick Verification Files

Several markdown files in the repo contain detailed test scenarios and architecture diagrams. When modifying loading, LOD, or rendering code, review:
- `TESTING_GUIDE.md`
- `VERIFICATION_CHECKLIST.md`
- `ARCHITECTURE_DIAGRAM.md`

---

## 12. Deployment Process

### 12.1 Vite Production Build

```bash
cd web
npm run build
```

This runs:
1. `npm run build:emcc` → `../build-web.sh`
2. `tsc` → TypeScript type checking
3. `vite build` → Bundles to `web/dist/`

### 12.2 Manual Deployment

`web/deploy.py` is a Paramiko-based SFTP uploader. It recursively uploads `web/dist/` to a remote server directory. **Do not commit credentials** — the script currently contains hardcoded values for reference; in production, use environment variables.

### 12.3 Asset Hosting Note

Runtime assets (textures, sounds) are expected to be served from the same origin as the deployed app, under `/solar-system/resource/`. The `build-web.sh` script intentionally **does not** mirror the full local `resource/` tree into `web/public/` because large DDS files are omitted from the repository. Ensure the deployment server hosts the complete asset tree.

---

## 13. Security Considerations

- **Same-Origin Policy:** The web build relies on same-origin or properly CORS-configured asset hosting. `WebResourceFetcher` uses `emscripten_async_wget2` and `emscripten_wget_data`, which are subject to browser CORS rules.
- **No Sensitive Data in Repo:** The repository does not contain API keys, passwords, or certificates. `deploy.py` contains example credentials that should be rotated if the target server is sensitive.
- **Memory Growth:** The WASM build allows memory growth (`ALLOW_MEMORY_GROWTH=1`) with an initial 256 MB and a 1 GB ceiling. This is necessary for large textures but should be monitored on low-end devices.
- **ASYNCIFY:** Enabled (`-s ASYNCIFY=1`) to support synchronous-looking fetches. This has a small code-size and runtime overhead; be cautious when adding additional sync-blocking calls.

---

## 14. Common Pitfalls (Do Not Do)

1. **Do not add `glew.h`** to the web build path. WebGL 2 headers are provided by Emscripten automatically.
2. **Do not use Geometry or Compute Shaders.** They are not supported in WebGL 2 (ES 3.0).
3. **Do not spawn threads** (`std::thread`) in the web build. Use `FPS_Handler` or frame-counters in the main loop to simulate background tasks (e.g., `UpdateSearchNearestPlanet`, `UpdateBackgroundMusic`).
4. **Do not use `glUniform1d` / `glUniform*dv`** on web builds. Always route through the `Shader` class, which handles the cast.
5. **Do not open HTML files with `file://`.** Serve via HTTP to avoid CORS and MEMFS issues.
6. **Do not preload sounds** in the CMake Emscripten block. The sound files are stored via Git LFS and may be absent, causing build failures.
7. **Do not request OpenGL 4.6 on web.** The web context is ES 3.0 (`GLFW_CONTEXT_VERSION_MAJOR=3, MINOR=0, GLFW_OPENGL_ES_API`).

---

## 15. Quick Reference: Adding a New Planet

1. Create `src/Solar_System/<PlanetName>_System/<PlanetName>.h` and `.cpp`, inheriting from `Planet`.
2. If the planet has moons, create `Satellite` subclasses in the same folder.
3. Add aggregate includes to `src/Solar_System/SolarSystem.h`.
4. In `Application.h`, add `Init<PlanetName>System(const MeshHolder&)` declaration.
5. In `Application.cpp`, implement the init method:
   - Use `GetTexturePath("resource/textures_low/...", "resource/textures/...")` for DDS textures.
   - Push a `RenderableSceneComponent` into `_renderableSceneComponents`.
6. Call the init method from `InitStarSystem()`:
   - Immediately for desktop.
   - Via `_planetSystemManifests` for all planets on WASM (assets are downloaded on demand).
7. If implementing LOD, override `LoadHighResIfClose()` in the planet class.

---

## 16. Cursor Cloud specific instructions

This section captures non-obvious, durable facts for running this project in a Cursor Cloud VM. The startup update script only refreshes the web frontend's npm deps (`npm install --prefix web`); all heavy toolchain/build state below is expected to persist in the VM snapshot.

### Runnable target
- **Only the web (WASM) target is runnable in this Linux cloud env.** The native desktop build (`./build.sh`) links Windows-only libs (`glew32`, `OpenGL32`, `irrKlang`, `mingw32`) and will not build/run here. Scope all work to the web target.

### Emscripten toolchain (preinstalled, persisted in snapshot)
- Emscripten SDK lives at `/content/build_space/emsdk` (version 6.0.0). `build-web.sh` and `setup_web_dependencies.sh` auto-source it from that path, so `./build-web.sh` works without arguments. If `emcc` is already on PATH you can also use `./build-web.sh --no-emsdk`.
- WASM dependencies (GLM headers + the Assimp static lib `external/assimp/build-wasm/lib/libassimp.a`) are prebuilt under `external/` and also persist in the snapshot. `setup_web_dependencies.sh` is idempotent and skips work if they already exist.
- If the toolchain or `external/` is ever missing, re-create with: `git clone https://github.com/emscripten-core/emsdk /content/build_space/emsdk && (cd /content/build_space/emsdk && ./emsdk install latest && ./emsdk activate latest)` then `./build-web.sh`.

### Build / lint / run (web)
- Rebuild WASM after C++/CMake changes: `./build-web.sh` (copies `SolarSystem.js` → `web/src/`, `SolarSystem.wasm`/`SolarSystem.data` → `web/public/`). This is the slow path; only needed for C++ changes.
- Frontend typecheck (closest thing to lint): `cd web && npx tsc`. Frontend bundle build: `cd web && npx vite build` (or `npm run build`, which first re-runs the full WASM build via `build:emcc`).
- **Run (dev):** `cd web && npm run dev` → open `http://localhost:5173/solar-system/` (note the `/solar-system/` base path; `/` alone 404s). TypeScript edits hot-reload; C++ edits require a `./build-web.sh` rebuild + refresh.
- **Run (preview/prod):** `cd web && npm run preview` → `http://localhost:4173/solar-system/` (serves the `dist/` build).
- No automated test suite exists; verification is manual/visual.

### Asset / rendering caveats (important — avoid false "broken" conclusions)
- Only **4×4 placeholder** low-res DDS textures are committed; the real planet textures and the skybox are hosted on a remote server (`https://test.1ink.us/solar-system/`) that is currently unreachable (404/CORS). **Therefore planet surfaces and the skybox do not render locally.** What *does* render and proves the engine works: the procedural **Sun** (corona, HDR glow, lens flare) and the 3D distance **labels**, plus full first-person camera navigation.
- In **dev** mode, `web/src/main.ts` forces all runtime asset fetches to the remote URL (`REMOTE_ASSET_BASE`), so even local placeholder textures are not used. **preview** mode uses the local base URL, so it fetches the bundled placeholder textures (copied to `web/public/resource/textures_low/`).
- The on-screen **FPS counter often reads 0**: this is `requestAnimationFrame` throttling when the canvas/tab is not focused, **not** a freeze. Confirm liveness by checking that labels move when you move the mouse / press WASD.
- Planets load lazily via staged loading only when the camera flies within their activation radius (800–1500 units) of huge orbital distances. For testing you can teleport instantly with the exposed JS binding `window.setCameraPose(x, y, z, yaw, pitch)` (the Sun is at the origin; yaw=0 looks +X, yaw=90 looks +Z).

*Last updated: 2026-06-22*

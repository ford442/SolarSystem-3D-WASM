# SolarSystem-3D-WASM: Copilot Instructions

## Quick Reference

**Project:** Hybrid C++17 3D Solar System renderer that runs natively (OpenGL 4.6) and in the browser (WebAssembly/WebGL 2).

**Key Goal:** Maintain 100% feature parity between Native and Web builds while respecting browser environment constraints.

## Build Commands

### Native Build
```bash
./build.sh
# Creates executable in ./build/ folder
```

### Web Build (WebAssembly)
```bash
# One-time setup
./setup_web_dependencies.sh

# Build & deploy
./build-web.sh
# Outputs: SolarSystem.wasm, SolarSystem.js, SolarSystem.data → web/public/

# Serve locally
cd build-web && python3 -m http.server 8000
# Open http://localhost:8000/SolarSystem.html
```

### Web Frontend
```bash
# From web/ directory
npm install
npm run build:emcc  # Runs build-web.sh
npm run build       # Full build (C++ → WASM + TypeScript)
npm run dev         # Vite dev server
```

**Note:** Web builds use Emscripten. Activate environment: `source /path/to/emsdk/emsdk_env.sh`

## Architecture

### Directory Structure
- **`/src`**
  - `Solar_System/`: Planet/satellite logic, physics, rendering (SpaceObject.h hierarchy)
  - `Auxiliary_Modules/`: Shaders, Camera, Mesh, Texture, FPS handling
  - `main.cpp, Application.cpp`: Entry point and main loop
- **`/resource`**: Shaders (GLSL), Textures (DDS), Models (OBJ), Fonts (TTF)
- **`/web`**: Frontend (HTML/CSS/TypeScript), Vite build config
- **`/external`**: Dependencies (GLM header-only, Assimp static lib for WASM)

### Data Flow
1. **C++ Runtime** compiles to WASM via Emscripten
2. **JavaScript Glue** (SolarSystem.js) marshals C↔JS calls
3. **Web Frontend** (TypeScript/HTML) loads WASM module and provides UI
4. **Assets** preloaded as .data file (shaders, fonts, icons) or lazy-loaded from `/solar-system/resource/`

## Key Conventions

### Platform-Specific Code
**Use `#ifdef __EMSCRIPTEN__` to separate logic:**

```cpp
#ifdef __EMSCRIPTEN__
  // Web: SDL_mixer audio, no threading
  Mix_PlayMusic(music, -1);
#else
  // Native: irrKlang, std::thread allowed
  engine->play2D(sound);
#endif
```

**Critical Patterns:**
- **Audio:** irrKlang (native) ↔ SDL_mixer (web)
- **Threading:** `std::thread` (native) ↔ Frame-based async (web, use `FPS_Handler`)
- **Graphics:** OpenGL 4.6 (native) ↔ WebGL 2/ES 3.0 (web)

### Main Loop
**NEVER use blocking loops in web build** – will freeze the browser.

```cpp
// ✅ Correct: Encapsulated logic
void Application::RunOneFrame() {
  Update();
  Render();
}

// Native: Traditional loop
while (!window.shouldClose) RunOneFrame();

// Web: Emscripten callback
emscripten_set_main_loop_arg(RunOneFrame, ...);
```

### Shader Compatibility

| Aspect | Native | Web |
|--------|--------|-----|
| Version | `#version 460 core` | `#version 300 es` |
| Fragment Precision | Optional | **MUST** include `precision highp float;` |
| Double Uniforms | `glUniform1d()` | Cast to float in `Shader.cpp` |
| Geometry/Compute | ✅ Supported | ❌ Not in WebGL 2 |

**Example (Shader.cpp):**
```cpp
#ifdef __EMSCRIPTEN__
  glUniform1f(loc, static_cast<float>(value));  // Cast double → float
#else
  glUniform1d(loc, value);
#endif
```

### Assets
- **Texture Format:** `.dds` only (nv_dds loader adapted for `std::istream`)
- **Preloading:** Shaders, fonts, icons preloaded to `.data` file
- **Runtime Fetch:** Large textures/models lazy-loaded from `/solar-system/resource/`

## Common Pitfalls

### ❌ Do NOT
1. **Add `glew.h`** to web build path → Use Emscripten's WebGL 2 headers
2. **Use Geometry/Compute Shaders** → Not in WebGL 2 (ES 3.0)
3. **Spawn `std::thread`** in web build → Use frame-counters in `RunOneFrame()` instead
4. **Use double uniforms directly** → Cast to float via `Shader.cpp` conditionals
5. **Block the main loop** → Freezes browser

### ✅ Do
1. Always gate platform-specific code with `#ifdef __EMSCRIPTEN__`
2. Add `precision highp float;` to all web fragment shaders
3. Use `FPS_Handler` to simulate background tasks on web
4. Test both builds regularly to ensure feature parity

## Reference Docs
- **[docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)** – Canonical system design (loading, LOD, assets)
- **[README.md](../README.md)** – Build, deployment, runtime asset hosting
- **[docs/plans/PORTING_GUIDE.md](../docs/plans/PORTING_GUIDE.md)** – WebAssembly port implementation details
- **[AGENTS.md](../AGENTS.md)** – Cursor Cloud / AI agent environment notes

## Common Tasks

### Modify Rendering Logic
1. Edit shaders in `/resource/shaders/`
2. Update in both native (`#version 460 core`) and web (`#version 300 es`) variants
3. Add `precision highp float;` to web fragment shaders
4. Rebuild: `./build-web.sh` (web) or `./build.sh` (native)

### Add Audio
```cpp
#ifdef __EMSCRIPTEN__
  SDL_mixer (web)
#else
  irrKlang (native)
#endif
```

### Background Task (e.g., nearest planet search)
- **Native:** Use `std::thread` (see `Application.cpp`)
- **Web:** Distribute work across frames using counters in `RunOneFrame()`

### Update Web Frontend
1. Modify files in `/web/src/`
2. Run `npm run build` from `/web/` directory
3. Outputs to `/web/dist/` (dev) or `/web/public/` (prod)

## Environment Notes
- **Emscripten Version:** Check `setup_web_dependencies.sh` (e.g., assimp v5.3.1)
- **CMake:** Version 3.18+ required
- **C++ Standard:** C++17
- **Memory (WASM):** 256MB initial, 1GB max (see CMakeLists.txt for tuning)

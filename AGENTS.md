# AGENTS.md

## Project Context
**SolarSystem-3D-WASM** is a hybrid C++17 application that runs natively (Windows/Linux via OpenGL 4.6) and in the browser (WebAssembly via WebGL 2/Emscripten).

**Core Goal:** Maintain 100% feature parity between the Native desktop build and the Web build while respecting the limitations of the browser environment (e.g., single-threaded loops, WebGL restrictions).

## Key Directives & Coding Standards

### 1. Platform conditionals
* **The Golden Rule:** Use `#ifdef __EMSCRIPTEN__` to separate platform logic.
* **Native:** Uses `irrKlang` for audio, `std::thread` for background tasks, and full OpenGL 4.6.
* **Web:** Uses `SDL_mixer` for audio, frame-based slicing for background tasks, and OpenGL ES 3.0.

### 2. The Main Loop (Critical)
* **NEVER** use a blocking `while (!WindowShouldClose)` loop for the web build. It will freeze the browser.
* **Pattern:** Logic must be encapsulated in `Application::RunOneFrame()`.
* **Implementation:**
    * Native: Calls `RunOneFrame()` inside a `while` loop.
    * Web: Passes `RunOneFrame` to `emscripten_set_main_loop_arg`.

### 3. Shader Compatibility
* **Version:** Native uses `#version 460 core`. Web uses `#version 300 es`.
* **Precision:** Web fragment shaders **MUST** include `precision highp float;`.
* **Data Types:** WebGL 2 does NOT support `double` precision uniforms. Always cast `double` to `float` before sending to shaders in `Shader.cpp`.

### 4. Audio Systems
* **Native:** Uses `irrKlang::ISoundEngine`.
* **Web:** Uses `SDL_mixer` (`Mix_Music`, `Mix_PlayMusic`).
* *Note:* Do not attempt to use `irrKlang` headers in the web build path; they are excluded from the include directories.

### 5. Memory & Assets
* **Preloading:** Assets in `resource/` are preloaded into a virtual file system. Use standard `std::ifstream` or `fopen` to read them; relative paths work as if local.
* **Texture Format:** Uses `.dds` textures. The `nv_dds` loader is adapted to work with `std::istream`.

## Directory Structure
* `/src`: C++ source code.
    * `/Solar_System`: Logic for planets, physics, and rendering.
    * `/Auxiliary_Modules`: Helpers for Shaders, Camera, and Textures.
* `/resource`: Assets (Shaders, Textures, Models, Fonts).
* `/web`: The web frontend (HTML/CSS/TS) and build artifacts destination.
* `/build-web`: The Emscripten build output directory.

## Available Tools & Commands

### 1. Build Tools
* **Build for Web (WASM):**
    * *Command:* `./build-web.sh`
    * *Description:* Compiles C++ to WASM, generates JS glue, and copies artifacts (`.wasm`, `.js`, `.data`) to `/web/public`.
* **Build for Native:**
    * *Command:* `./build.sh`
    * *Description:* Uses Ninja/CMake to build the desktop executable.
* **Dependency Setup:**
    * *Command:* `./setup_web_dependencies.sh`
    * *Description:* Fetches and compiles `libassimp.a` for Emscripten.

### 2. Running & Testing
* **Serve Web Build:**
    * *Command:* `cd build-web && python3 -m http.server 8000`
    * *Action:* Open `http://localhost:8000/SolarSystem.html`.
    * *Note:* Direct file opening (`file://`) will fail due to CORS.

## Common Pitfalls (Do Not Do)
1.  **Do not add `glew.h`** to the web build path. WebGL 2 headers are provided by Emscripten automatically.
2.  **Do not use Geometry or Compute Shaders.** They are not supported in WebGL 2 (ES 3.0).
3.  **Do not spawn threads** (`std::thread`) in the web build. Use `FPS_Handler` or frame-counters in the main loop to simulate background tasks (e.g., `UpdateSearchNearestPlanet`).

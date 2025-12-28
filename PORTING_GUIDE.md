# Porting to WebAssembly (Emscripten)

This guide outlines the steps required to convert this OpenGL 4.6 application to run in a web browser using Emscripten.

## 1. Feasibility Analysis

*   **Graphics**: The project uses OpenGL 4.6 but currently **does not** appear to utilize Geometry, Tessellation, or Compute shaders in the provided shader files. This makes a port to **WebGL 2 (OpenGL ES 3.0)** highly feasible without a complete rewrite.
    *   *Constraint*: WebGL 2 does not support Double Precision floats (`double`) in shaders. All `glUniform1d`, `glUniformMatrix4dv`, etc., must be converted to `float`.
*   **Windowing**: The project uses GLFW, which has excellent Emscripten support. SDL2 is currently linked but barely used (only for loading the window icon).
*   **Audio**: `irrKlang` is not compatible with the web. It must be replaced (e.g., with SDL_mixer or OpenAL).
*   **Threading**: The project uses `std::thread`. Emscripten supports pthreads (via SharedArrayBuffer), but for this project, refactoring to single-threaded async loops might be simpler and more compatible.

## 2. Build System (CMake)

You will need to use the Emscripten CMake toolchain.

### Command
```bash
emcmake cmake -B build-web .
cd build-web
emmake make
```

### CMakeLists.txt Changes
You need to handle dependencies that are built-in to Emscripten (SDL2, GLFW) and exclude incompatible ones (irrKlang, native GLEW).

```cmake
if(EMSCRIPTEN)
    # Emscripten flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_GLFW=3 -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s WASM=1 -s FULL_ES3=1")

    # Enable S3TC compression for DDS textures
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WEBGL2_BACKEND=1")

    # Allow memory growth
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1")

    # Linker flags for preloading assets
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --preload-file ../resource@/resource")
else()
    # Original dependencies
    target_link_libraries(${PROJECT_NAME} glfw3 glew32 irrKlang ...)
endif()
```

## 3. Code Modifications

### A. Main Loop
Web browsers cannot block on a `while` loop. You must use `emscripten_set_main_loop`.

**Current:**
```cpp
while (!glfwWindowShouldClose(_mainWindow)) {
    // Render code
}
```

**Target:**
```cpp
#include <emscripten.h>

void loop_iteration(void* arg) {
    Application* app = (Application*)arg;
    app->RunOneFrame(); // You need to extract the loop body into a function
}

// In main():
emscripten_set_main_loop_arg(loop_iteration, &application, 0, 1);
```

### B. Shaders & Double Precision
WebGL shaders do not support `double`.
1.  **C++**: In `src/Auxiliary_Modules/Shader.cpp`, any call to `glUniform1d`, `glUniform3dv`, etc., will fail or do nothing. You must cast `double` values to `float` before passing them to the shader using `glUniform1f`, `glUniform3fv`.
2.  **GLSL**: Ensure shaders use `#version 300 es` for WebGL 2. Remove `precision double` qualifiers.

### C. Audio (irrKlang Replacement)
Replace `irrKlang` with **SDL_mixer**. Emscripten has a built-in port (`-s USE_SDL_MIXER=2`).
*   Rewrite `Application::StartPlayBackgroundMusic` to use `Mix_PlayMusic` and `Mix_HookMusicFinished`.

### D. Threading
The project uses threads for:
1.  **Music Fading**: Use `emscripten_set_interval` or simple per-frame logic to fade volume.
2.  **Nearest Planet Search**: This loop is likely fast enough to run on the main thread every few seconds, or use `pthreads` if you can configure the server headers (COOP/COEP).
    *   *Recommendation*: Move the nearest planet search to the main loop, running only every 60 frames.

### E. Assets (DDS Textures)
The code uses `nv_dds` to load DDS files.
*   **Preloading**: Use `--preload-file resource` in CMake. The `std::ifstream` in `nv_dds.cpp` will then work transparently.
*   **Extension**: You must enable `WEBGL_compressed_texture_s3tc` in your JS/Emscripten initialization, or ensure the browser supports it. Emscripten's `-s FULL_ES3=1` usually handles context creation, but you might need to request the extension explicitly if `nv_dds` checks for it via `glGetString`.

## 4. Dependencies Strategy

| Dependency | Web Status | Action |
| :--- | :--- | :--- |
| **GLFW** | Supported | Use `-s USE_GLFW=3`. |
| **SDL2** | Supported | Use `-s USE_SDL=2`. Note: You are mixing SDL and GLFW. Recommended to stick to GLFW for windowing and remove SDL_Init, or switch entirely to SDL. |
| **GLEW** | Not Needed | Emscripten provides headers. Remove for web build. |
| **irrKlang** | **Unsupported** | **Replace with SDL_mixer.** |
| **Assimp** | Supported | Compile from source or use Emscripten ports if available. Warning: Large binary size. |
| **FreeType** | Supported | Compile from source or use Emscripten ports (`-s USE_FREETYPE=1`). |

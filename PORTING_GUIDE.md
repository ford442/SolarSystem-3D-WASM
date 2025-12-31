# Porting to WebAssembly (Emscripten)

This guide outlines the steps required to convert this OpenGL 4.6 application to run in a web browser using Emscripten.

## Status: ✅ COMPLETED

This project has been successfully ported to WebAssembly! The following changes have been implemented:

## 1. Build Instructions

### Prerequisites
- Install [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
- Activate the Emscripten environment: `source /path/to/emsdk/emsdk_env.sh`

### Build Commands
```bash
emcmake cmake -B build-web .
cd build-web
emmake make
```

This will generate:
- `SolarSystem.html` - The main HTML file to open in a browser
- `SolarSystem.js` - The JavaScript glue code
- `SolarSystem.wasm` - The WebAssembly binary
- `SolarSystem.data` - Preloaded assets (textures, models, etc.)

### Running
Open `SolarSystem.html` in a web browser. Note: Due to CORS restrictions, you may need to serve it via a local web server:
```bash
python3 -m http.server 8000
# Then open http://localhost:8000/SolarSystem.html
```

## 2. Implementation Summary

### ✅ CMakeLists.txt Changes
The build system now properly handles Emscripten-specific configuration:
- Uses `-s USE_GLFW=3` for GLFW support
- Uses `-s USE_SDL=2` and `-s USE_SDL_IMAGE=2` for SDL support
- Uses `-s USE_SDL_MIXER=2` for audio (replacement for irrKlang)
- Uses `-s USE_FREETYPE=1` for text rendering
- Uses `-s FULL_ES3=1` for WebGL 2.0 support
- Preloads assets with `--preload-file resource@/resource`
- Conditionally excludes GLEW and irrKlang for web builds

### ✅ Main Loop Refactoring
The blocking `while` loop has been converted to work with Emscripten:
- Added `Application::RunOneFrame()` method that contains the loop body
- `Application::Exec()` now uses `emscripten_set_main_loop_arg()` for web builds
- Native builds continue to use the traditional blocking loop

### ✅ Shader Updates
All shaders have been updated to WebGL 2.0 (OpenGL ES 3.0):
- Changed from `#version 460 core` to `#version 300 es`
- Added precision qualifiers to fragment shaders: `precision highp float;`
- No double precision types were used in shaders (already using float)

### ✅ Double Precision Uniform Handling
The `Shader.cpp` file conditionally compiles uniform methods:
- Under `__EMSCRIPTEN__`, double precision uniforms are cast to float
- All `glUniform*d` calls use `glUniform*f` equivalents on web
- Native builds continue to use double precision

### ✅ Audio System Replacement
irrKlang has been replaced with SDL_mixer for web compatibility:
- Native builds continue to use irrKlang
- Web builds use SDL_mixer with `Mix_Music` and `Mix_PlayMusic`
- Volume control and fade in/out implemented using SDL_mixer API
- Music playback managed in the main loop for web builds

### ✅ Threading Refactoring
Threads have been converted to frame-based logic for web compatibility:

**Nearest Planet Search:**
- Native: Uses `std::thread` running continuously
- Web: Runs every 60 frames (~1 second at 60fps) in `UpdateSearchNearestPlanet()`

**Background Music:**
- Native: Uses `std::thread` with volume fading logic
- Web: Managed per-frame in `UpdateBackgroundMusic()` with SDL_mixer

### ✅ OpenGL Context Configuration
The initialization code properly handles both platforms:
- Native: Requests OpenGL 4.6 Core Profile
- Web: Requests OpenGL ES 3.0 (WebGL 2)
- GLEW initialization is skipped on web (not needed)
- `GL_POLYGON_SMOOTH` is disabled on web (not supported)

## 3. Technical Details

### Feasibility Analysis
*   **Graphics**: The project uses OpenGL 4.6 but does not utilize Geometry, Tessellation, or Compute shaders. Port to WebGL 2 (OpenGL ES 3.0) is fully compatible.
    *   WebGL 2 does not support double precision floats - handled via conditional compilation.
*   **Windowing**: GLFW has excellent Emscripten support - works out of the box.
*   **Audio**: irrKlang is not web-compatible - successfully replaced with SDL_mixer.
*   **Threading**: `std::thread` replaced with frame-based logic for web builds.

### Assets (DDS Textures)
The code uses `nv_dds` to load DDS files:
*   Assets are preloaded using `--preload-file` flag
*   `std::ifstream` in `nv_dds.cpp` works transparently with Emscripten's virtual filesystem
*   WebGL 2 supports compressed texture formats via extensions

## 4. Platform-Specific Code

The codebase uses `#ifdef __EMSCRIPTEN__` to conditionally compile platform-specific code:

```cpp
#ifdef __EMSCRIPTEN__
    // Web-specific code (SDL_mixer, frame-based logic, etc.)
#else
    // Native code (irrKlang, threads, etc.)
#endif
```

## 5. Dependencies

| Dependency | Native | Web | Status |
| :--- | :--- | :--- | :--- |
| **GLFW** | ✅ Linked | ✅ `-s USE_GLFW=3` | Working |
| **SDL2** | ✅ Linked | ✅ `-s USE_SDL=2` | Working |
| **SDL_image** | ✅ Linked | ✅ `-s USE_SDL_IMAGE=2` | Working |
| **SDL_mixer** | ❌ Not used | ✅ `-s USE_SDL_MIXER=2` | Working |
| **GLEW** | ✅ Required | ❌ Not needed | N/A on web |
| **irrKlang** | ✅ Audio engine | ❌ Unsupported | Replaced |
| **Assimp** | ✅ Linked | ✅ Build from source | Required |
| **FreeType** | ✅ Linked | ✅ `-s USE_FREETYPE=1` | Working |

## 6. Known Limitations

- Music fade in/out is simplified on web (no exponential curves)
- Nearest planet search runs less frequently on web (every 60 frames vs continuous)
- Some OpenGL features like `GL_POLYGON_SMOOTH` are not available in WebGL 2
- Large asset files may take time to download and preload

## 7. Future Improvements

Potential enhancements for the web version:
- Add loading progress indicator during asset preloading
- Optimize asset sizes (compress textures, reduce model complexity)
- Add mobile touch controls
- Implement more sophisticated audio streaming for background music
- Use Web Workers for background tasks (if needed)

# Porting to WebAssembly (Emscripten)

This guide outlines the steps required to convert this OpenGL 4.6 application to run in a web browser using Emscripten.

## Status: ✅ COMPLETED

This project has been successfully ported to WebAssembly! The following changes have been implemented:

## 1. Build Instructions

### Prerequisites
- Install [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
- Activate the Emscripten environment: `source /path/to/emsdk/emsdk_env.sh`
- Setup Dependencies:
  ```bash
  ./setup_web_dependencies.sh
  ```
  This script fetches GLM and builds a static Assimp library (`libassimp.a`) optimized for the web.

### Build Commands
```bash
./build-web.sh
```

This will generate:
- `SolarSystem.js` - Emscripten's ES module glue code (`MODULARIZE=1` + `EXPORT_ES6=1`; no standalone `SolarSystem.html` shell is produced)
- `SolarSystem.wasm` - The WebAssembly binary
- `SolarSystem.data` - Preloaded assets (shaders, fonts, icons, low-res textures)
- It deploys `SolarSystem.js` to `web/src/` and `SolarSystem.wasm` / `SolarSystem.data` to `web/public/`.

### Running
The Vite app in `web/` imports `SolarSystem.js` as a module — there is no raw HTML shell to open directly.
```bash
cd web
npm install
npm run dev      # dev server, served under the /solar-system/ base path
# or, for a production-shaped build:
npm run build && npm run preview
```
Then open `http://localhost:5173/solar-system/` (dev) or the `preview` URL Vite prints.

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

## 3b. Emscripten build flags (CMake)

Flags live in named lists in `CMakeLists.txt` and are applied **once** via
`target_compile_options` / `target_link_options` (no duplicated `-s` settings).

| Config | Opts |
| :--- | :--- |
| **Release** (default via `./build-web.sh`) | `-O3 -flto` (no `--closure 1` yet — see below) |
| **Debug** (`./build-web.sh --debug`) | `-O0 -g`, link `-sASSERTIONS=1` |

Other important settings: `ASYNCIFY=1` (for `emscripten_wget_data`), `ALLOW_MEMORY_GROWTH=1`,
`GROWABLE_ARRAYBUFFERS=0` (explicit — see "Memory / heap views" below),
`INITIAL_MEMORY=256MB` / `MAXIMUM_MEMORY=1GB`, `MODULARIZE=1` + `EXPORT_ES6=1`.
JS control surface uses `EMSCRIPTEN_KEEPALIVE` in `WasmExports.cpp`. CMake passes
`EXPORTED_FUNCTIONS=@scripts/wasm-exports.json` (generated by
`scripts/generate-wasm-exports.mjs`, verified in CI via `npm run generate:wasm-exports:check`
alongside the TypeScript bindings it also emits) plus `EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPF32']`.

### Async fetch: ASYNCIFY vs JSPI (evaluated 2026-09-07)

`WebResourceFetcher::Fetch()` blocks on `emscripten_wget_data()` from several call
sites that are not JS-driven entry points (`SkyBox`/`MeshHolder` constructors,
`Application::LoadPlanetSystemManifests()`, the initial `TextureImage2D` load) —
this is why `ASYNCIFY=1` is required today.

Emscripten 6 implements `-sJSPI=1` as `ASYNCIFY=2` (a newer, browser-native
stack-switching mechanism), and it was tried as a drop-in replacement:

| Artifact | `ASYNCIFY=1` | `-sJSPI=1` |
| :--- | ---: | ---: |
| `SolarSystem.wasm` | 3 972 203 B | 2 815 331 B (-29%) |
| `SolarSystem.js` | 195 516 B | 192 004 B |

The size win is real, but **it does not work out of the box**: at runtime, Chromium
throws `SuspendError: trying to suspend without WebAssembly.promising`. Emscripten
only wraps `main()` in `WebAssembly.promising` by default (`JSPI_EXPORTS=['main']`);
the actual suspension happens later, from `emscripten_set_main_loop`'s per-frame
callback and from lazy texture/mesh loads triggered outside `main()`'s synchronous
body — call chains JSPI does not automatically cover.

Making JSPI work would require the same restructuring as Option A in the original
proposal (issue tracked separately): move `WebResourceFetcher` off blocking
`emscripten_wget_data` entirely (JS `fetch()` writing into MEMFS, C++ only reading
already-resident files — the pattern `WebResourceFetcher::DownloadFile` /
`TextureLoadingQueue` already use via `emscripten_async_wget2`, which is
callback-based and never needed ASYNCIFY). Until that lands, **`ASYNCIFY=1` stays
the default**; revisit `-sJSPI=1` once every blocking `Fetch()` call site is gone.

### Native Wasm exceptions

This project needs **`ASYNCIFY=1`** for `emscripten_wget_data`. Emscripten 6.x reports
`ASYNCIFY=1 is not compatible with -fwasm-exceptions`, so Release/Debug web builds keep
the JS exception path: **`-sDISABLE_EXCEPTION_CATCHING=0`** (applied once on compile and link).

If ASYNCIFY is ever removed (e.g. the JSPI migration above), revisit `-fwasm-exceptions`
for smaller/faster exception code. Browser floor for Wasm exceptions: Chrome 95+, Firefox 100+, Safari 15.2+.

### `--closure 1`

Not yet enabled. The original blocker — `EM_ASM` bridges reading `window.updateLoadingProgress`
and friends — is gone (post-init JS callbacks now go through `Module.onSettingsChanged` /
`Module.updateLoadingProgress` / `Module.updateStreamingProgress` / `Module.onPlanetFocused`,
set from `web/src/wasmCallbacks.ts`, not `window.*`). What remains is `EM_ASM`/`EM_JS` in
`JsBridge.cpp` (reads those same `Module.*` callbacks), `QualitySettings.cpp` and
`WebResourceFetcher.cpp` (read `window.__solarSystemInit` / `window.__solarSystemAssetBase`
before `main()` runs), and `TextureLoadingQueue.cpp` (now `emscripten_get_heap_size()`, no
`EM_ASM` left there). Closure's property renaming could still mangle the `Module.foo`-style
property reads inside the remaining `EM_ASM` blocks in `JsBridge.cpp` unless those names are
added to closure externs. Trying `--closure 1` and fixing the fallout is worth a follow-up,
but has not been measured — leaving it explicitly rejected for now rather than blindly enabled.

### Memory / heap views

`GROWABLE_ARRAYBUFFERS` defaults to `0` (fixed-buffer growth: a new `ArrayBuffer` plus a
heap-view refresh on grow) unless explicitly raised to `1`/`2` (resizable `ArrayBuffer`,
via `WebAssembly.Memory.prototype.toResizableBuffer()`). This project now sets
`GROWABLE_ARRAYBUFFERS=0` explicitly in `CMakeLists.txt` to document that choice, which
means `web/src/bootstrap.ts` no longer needs to monkey-patch `toResizableBuffer` off the
`WebAssembly.Memory` prototype — the generated runtime never calls it at this setting, and
the patch has been removed.

`TextureLoadingQueue.cpp`'s memory-pressure check now calls `emscripten_get_heap_size()`
(from `<emscripten/heap.h>`) instead of `EM_ASM_INT({ return HEAP8.length; })`.

### Artifact size baseline

Measured on this tree before/after the 2026-07-21 flag cleanup (`-O3 -flto`, deduped `-s`
flags), and again after the 2026-09-07 explicit-`EXPORTED_FUNCTIONS` change (Release,
`ASYNCIFY=1`):

| Artifact | Before flag cleanup | After flag cleanup (2026-07-21) | Explicit `EXPORTED_FUNCTIONS` (2026-09-07) |
| :--- | ---: | ---: | ---: |
| `SolarSystem.wasm` | 12 373 268 B | 3 874 669 B (−69%) | 3 972 203 B |
| `SolarSystem.js` | 569 186 B | 191 040 B (−66%) | 195 516 B |

The small increase from the explicit export list (vs. the old bare `['_main']`) is expected:
every `EMSCRIPTEN_KEEPALIVE` export now also gets a generated JS wrapper on `Module`, not just
whatever the linker happened to keep reachable. It buys an auditable, CI-checked export
surface (a prerequisite for `--closure 1`), which is worth the ~2.5% size cost.

Wipe `build-web/` when switching Debug↔Release or changing LTO/exception/ASYNCIFY flags.

## 3c. WebGL context creation, resize/DPI, and capability probing (2026-09-07)

### Context attributes: `Module.contextAttributes` does nothing

Emscripten's GLFW port builds its **own** WebGL context-attributes object purely from GLFW
window hints (`antialias` from `GLFW_SAMPLES`, `depth`/`stencil`/`alpha` from their matching
hints) and passes that to `Browser.createContext()`. It never reads `Module.contextAttributes`
— there is no such hook in Emscripten's runtime. A `contextAttributes` field used to exist on
`SolarSystemModuleConfig` (with `powerPreference`, `xrCompatible`, `premultipliedAlpha`,
`preserveDrawingBuffer`, etc.) and was a complete no-op.

The fix: `web/src/bootstrap.ts` creates the WebGL2 context **itself** —
`canvas.getContext('webgl2', contextOptions)` — with every attribute it actually wants, and
hands it to Emscripten via `Module.preinitializedWebGLContext`. `Browser.createContext()`
detects that key and reuses the existing context instead of creating its own, so every
attribute (including ones GLFW hints can't express) takes effect. This key must be listed in
`-sINCOMING_MODULE_JS_API` (see `CMakeLists.txt`) or Emscripten strips the code path that
reads it at build time.

`web/src/webglContext.ts` is the single owner of this decision, keyed off one three-way
`WebGlCostTier` (`lowMobile` / `medium` / `full`) mirroring `GetQualitySettings()`'s existing
mobile-forces-cheapest-tier rule (`requestedMsaaSamples` is already 0 for every mobile preset
and only 4 for desktop "full" — see `QualitySettings.cpp`), so `antialias` here can't drift
from the MSAA the C++ quality tier already assumed:

| Attribute | lowMobile | medium | full |
| :--- | :--- | :--- | :--- |
| `powerPreference` | `low-power` | `high-performance` | `high-performance` |
| `alpha` | `false` | `false` | `false` |
| `antialias` | `false` | `false` | `true` |
| backing-store scale | `min(dpr, 1)` | `min(dpr, 1.5)` | `min(dpr, 2)` |

`bootstrap.ts` logs both the requested and actual (`gl.getContextAttributes()`) attributes to
the console on load. `GLFW_SAMPLES` is no longer set on web (it would be ignored anyway once a
context is preinitialized); the quality-preset MSAA logging in `QualitySettings.cpp` still
applies (WebGL contexts can't be recreated, so MSAA still needs a page reload to change).

### Resize / DPI

Two independent bugs, now fixed:

1. **Initial size used the monitor's screen resolution**, not the canvas's actual viewport
   size (`glfwGetVideoMode(glfwGetPrimaryMonitor())` returns `screen.width/height`, which can
   differ from the real viewport — browser chrome, embedding, multi-monitor). `Application::InitSystems()`
   now sizes the window from `emscripten_get_element_css_size("#canvas", ...)` times the
   `backingStoreScale` computed above, and clears the inline CSS width/height GLFW forces onto
   the canvas afterward (`canvas.style.removeProperty(...)`) so `index.html`'s
   `width:100vw;height:100vh` governs on-page layout while `canvas.width/height` (the backing
   store) stays at the capped-DPR resolution.
2. **Nothing resized anything on a real browser window resize.** Two separate causes:
   - Emscripten's GLFW port never wires a browser `resize` event to
     `glfwSetFramebufferSizeCallback` on its own — that callback only fires when something
     explicitly calls `Browser.setCanvasSize()` (only the fullscreen enter/exit path does).
     Fixed by registering `emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, ...)`
     directly (`Application::WebWindowResizeCallback`), which listens for real window resize
     and orientation-change events independent of GLFW's internal (opt-in-only) plumbing.
   - Even when the callback fires, `FramebufferSizeCallback` only called `glViewport()` — it
     never updated `_displayWidth/_displayHeight`, camera aspect, or resized the HDR/bloom
     FBOs. `Application::HandleResize()` now does all of that (with a same-size early-out so
     redundant resize events are cheap), and both the web resize callback and the native/GLFW
     framebuffer-size callback funnel into it.

### GL capability probing (`GlCapabilities.cpp`)

`GetGlCapabilities()` probes anisotropic filtering, `WEBGL_compressed_texture_s3tc`,
`EXT_color_buffer_float`, and `GL_MAX_TEXTURE_SIZE` once (cached for the process) right after
`glfwMakeContextCurrent()`, and logs the result. Two things worth knowing if you touch this:

- **`GL_TEXTURE_MAX_ANISOTROPY_EXT` and `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` are not declared
  by Emscripten's GLES3 headers or its `GL/glew.h` shim**, and `#include <GLES2/gl2ext.h>`
  conflicts with that shim's own typedefs when included alongside it (redefinition errors).
  `GlCapabilities.h` just `#define`s the two stable, spec-fixed enum values directly instead of
  fighting the header conflict.
- **`glfwExtensionSupported()` is broken in Emscripten 6.0.3**: its JS shim
  (`library_glfw.js`) never calls `UTF8ToString()` on the incoming `const char*` before
  comparing it against the extension array, so it always compares a raw pointer against
  strings and never matches — it silently reports every extension as unsupported. Worked
  around with a direct `EM_ASM_INT` call in `GlCapabilities.cpp`
  (`IsWebGlExtensionSupported`) that does the `UTF8ToString()` conversion itself against
  `GLctx.getSupportedExtensions()`. Re-check this if upgrading Emscripten — the bug may be
  fixed upstream by then, but the direct-query workaround will keep working regardless.

Anisotropic filtering is now enabled on web when the extension exists (capped 4× mobile / 8×
desktop, vs. native's unconditional 16×). DDS loading throws a specific "S3TC not supported"
error (instead of a generic parse failure) when a compressed texture is loaded on a GPU/browser
without `WEBGL_compressed_texture_s3tc` — it still falls back to the same checkerboard
placeholder texture as any other load failure, just with a clearer console message.

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
| **Assimp** | ✅ Linked | ✅ Build from source | Required (`setup_web_dependencies.sh`) |
| **FreeType** | ✅ Linked | ✅ `-s USE_FREETYPE=1` | Working |

## 6. Known Limitations

- Music fade in/out is simplified on web (no exponential curves)
- Nearest planet search runs less frequently on web (every 60 frames vs continuous)
- Some OpenGL features like `GL_POLYGON_SMOOTH` are not available in WebGL 2
- Large asset files may take time to download and preload

## 7. WebGPU & Future Improvements

This section details considerations for a future migration to WebGPU.

### WebGPU Transition Strategy
Moving to WebGPU would offer lower overhead and access to modern GPU features like Compute Shaders, which could accelerate the "Nearest Planet Search" and physics simulations.

1.  **Shaders (WGSL):**
    *   WebGPU uses WGSL (WebGPU Shading Language), not GLSL.
    *   **Migration:** All `.fs` and `.vs` files would need to be rewritten in WGSL or transpiled using a tool like [Naga](https://github.com/gfx-rs/naga).
    *   **Uniforms:** Uniform buffers in WebGPU are stricter (requires padding/alignment). The `Shader` class would need significant refactoring to manage `wgpu::BindGroup` and `wgpu::Buffer`.

2.  **Pipeline State:**
    *   OpenGL is a state machine (e.g., `glEnable(GL_DEPTH_TEST)`).
    *   WebGPU is pipeline-based. You create a `RenderPipeline` object that encapsulates all state (blend modes, depth stencil, vertex formats) upfront.
    *   **Migration:** The `Application` class would need to pre-create Pipelines for different rendering passes (e.g., Planet Pass, Star Pass, Text Pass).

3.  **Compute Shaders:**
    *   The `UpdateSearchNearestPlanet` logic is currently on the CPU (threaded or framed).
    *   **Opportunity:** This could be moved to a WebGPU Compute Shader, running efficiently on the GPU every frame without stalling the main thread.

4.  **Assimp & Assets:**
    *   Loading meshes remains the same, but uploading them to the GPU changes from `glGenBuffers`/`glBufferData` to `device.createBuffer` and `queue.writeBuffer`.

5.  **Emscripten WebGPU:**
    *   Emscripten has experimental support for WebGPU (`-s USE_WEBGPU=1`). It maps C++ `wgpu.h` calls to the JS API.
    *   This is the recommended path for C++ projects.

### Other Future Improvements
- Add loading progress indicator during asset preloading
- Optimize asset sizes (compress textures, reduce model complexity)
- Add mobile touch controls
- Implement more sophisticated audio streaming for background music
- Use Web Workers for background tasks (if needed)

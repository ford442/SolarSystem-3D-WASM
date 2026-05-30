# grok.md

## Project Overview
**SolarSystem-3D-WASM** is a 3D solar system simulator built in C++ and compiled to WebAssembly using Emscripten. It renders planets with high-quality textures (DDS), supports level-of-detail (LOD) texture streaming, and aims for efficient browser-based 3D rendering using WebGL 2.

The project focuses on performance in the browser, especially around texture loading, mipmap handling, and asynchronous asset streaming.

## Tech Stack
- **Language**: C++17
- **Graphics**: OpenGL ES 3.0 / WebGL 2 (via Emscripten)
- **Build System**: Emscripten (`em++`)
- **Texture Format**: DDS (with custom loader in `nv_dds.cpp`)
- **Key Components**:
  - `Planet` class with LOD logic (`LoadHighResIfClose`)
  - `WebResourceFetcher` for async downloads
  - Custom polyfills for WebGL limitations (e.g. `glBindTextureUnit`)
  - Emscripten virtual filesystem for asset management

## Build & Run
```bash
./build-web.sh
After building, serve the output (usually build/ or public/) using a local server:
Bashpython3 -m http.server 8080
Then open index.html in a modern browser (Chrome recommended).
Key Files & Architecture





























PathPurposesrc/Solar_System/Planet.cppPlanet rendering + LOD logicsrc/nv_dds.cppDDS texture loadingsrc/SystemModules.hWebGL polyfills & Emscripten helperssrc/WebResourceFetcher.*Async file downloading from the websrc/main.cppScene setup and main loop
Current Focus Areas (as of May 2026)

Fixing black/missing textures in WebAssembly builds
Implementing proper GL_TEXTURE_MAX_LEVEL handling for DDS mipmaps
Building an async background texture streaming system (high-res textures only load when the camera is close)
Improving LoadHighResIfClose() to work reliably with emscripten_async_wget2

Known Issues & Gotchas

Black textures: Usually caused by incomplete mipmap chains. Always set GL_TEXTURE_MAX_LEVEL.
LOD / High-res loading: The current system tries to swap textures too early. High-res files are not preloaded, so async fetching is required.
WebGL 2 compatibility: Several OpenGL features need polyfills or workarounds in Emscripten.
Direct geometry/vertex mutation after loading models can cause scaling explosions (use THREE.Group wrappers in JS, or proper transforms in C++).

Collaboration Guidelines

When modifying texture loading or DDS handling, always test in WebAssembly — desktop OpenGL often hides mipmap and binding issues.
Prefer async loading patterns using WebResourceFetcher + Emscripten’s async wget functions for large assets.
Keep the main thread responsive — avoid blocking downloads during rendering.
Document any new polyfills or WebGL workarounds in SystemModules.h.

Preferred Workflow

Make changes in C++
Run ./build-web.sh
Serve locally and test in Chrome
Check browser console + WebGL inspector for texture binding or mipmap errors
When adding new features, consider whether they should work in both native and WASM builds (use #ifdef __EMSCRIPTEN__ where needed)

Future Goals

Robust background downloading of high-resolution planet textures
Better LOD system with progress indicators
Improved texture memory management
Cleaner separation between low-res and high-res asset pipelines


Would you like me to adjust anything in this grok.md (for example, add more technical depth, change the tone, or include specific build commands / folder structure)?GitHub taht is good. add it to the repo✅ Here's the grok.md file ready to add to your repo.
Instructions:

Create a new file in the root of ford442/SolarSystem-3D-WASM called grok.md
Paste the content below into it
Commit and push


Markdown# grok.md

## Project Overview
**SolarSystem-3D-WASM** is a 3D solar system simulator built in C++ and compiled to WebAssembly using Emscripten. It renders planets with high-quality textures (DDS), supports level-of-detail (LOD) texture streaming, and focuses on efficient browser-based 3D rendering using WebGL 2.

The main goal is high-performance planet rendering in the browser while managing bandwidth through smart texture loading.

## Tech Stack
- **Language**: C++17
- **Graphics API**: OpenGL ES 3.0 → WebGL 2 (via Emscripten)
- **Build Tool**: Emscripten (`em++`)
- **Texture Format**: DDS (custom loader in `nv_dds.cpp`)
- **Key Systems**:
  - `Planet` class with LOD logic (`LoadHighResIfClose`)
  - `WebResourceFetcher` for async asset downloading
  - WebGL compatibility polyfills (`SystemModules.h`)
  - Emscripten virtual filesystem

## Build Instructions

```bash
./build-web.sh
After building, serve the output folder locally:
Bashpython3 -m http.server 8080
Then open index.html in Chrome (recommended).
Important Files




FileResponsibilitysrc/Solar_System/Planet.cppPlanet rendering + LOD systemsrc/nv_dds.cppDDS texture loading & mipmap handlingsrc/SystemModules.hWebGL polyfills and Emscripten helperssrc/WebResourceFetcher.*Async file downloadingsrc/main.cppScene setup and main loop
Current Priorities (May 2026)

Fixing black/missing planet and moon textures in WebAssembly
Adding GL_TEXTURE_MAX_LEVEL to prevent incomplete mipmap chain errors
Building a proper async background texture streaming system (high-res textures load only when close)
Improving LoadHighResIfClose() to work reliably with Emscripten

Known Issues & Patterns

Black textures — Almost always caused by missing GL_TEXTURE_MAX_LEVEL when loading DDS files in WebGL 2.
LOD problems — High-res textures are not preloaded. The current LoadHighResIfClose() logic fails in WASM because the files don’t exist yet.
Direct mutation of geometry/vertices after loading can cause major scaling issues. Prefer using transformation groups/wrappers.
WebGL has limitations compared to desktop OpenGL — always test changes in the WebAssembly build.

## DDS Texture Requirements (WebGL 2 / WASM builds)

The following constraints apply to DDS assets served to the WASM/WebGL 2 target:

- **Format**: DXT1 (`GL_COMPRESSED_RGBA_S3TC_DXT1_EXT`), DXT3, or DXT5.  Uncompressed RGB/RGBA DDS are also supported (RGB is auto-converted to RGBA on upload).
- **Dimensions**: Base level must be a multiple of 4 in each dimension (S3TC block size). Textures smaller than 4px are padded automatically during upload.
- **Mipmap chain**: For DXT textures the mip chain can stop at 4×4 (one block) instead of 1×1. `GL_TEXTURE_MAX_LEVEL` is set explicitly to the last uploaded level so WebGL 2 does not consider the texture incomplete.
- **`GL_TEXTURE_MAX_LEVEL` rule**: Every 2D texture and cubemap face must have `BASE_LEVEL` and `MAX_LEVEL` declared to match the levels that were actually uploaded.  `nv_dds` does this automatically in `upload_texture2D()`.  After `glGenerateMipmap` (uncompressed textures with no embedded mips), `TextureImage2D` recalculates and re-declares MAX_LEVEL.
- **Cubemap / skybox**: The skybox uses `GL_LINEAR` (no mipmaps). `GL_TEXTURE_MAX_LEVEL = 0` is set after all six faces are uploaded to prevent any per-face residual from making the cubemap incomplete.
- **Missing assets**: `TextureImage2D` generates a 4×4 grey checker fallback so the scene never renders a black or unbound texture when a DDS file is absent.

Collaboration Guidelines

Always test in WebAssembly when working on textures, shaders, or rendering. Desktop builds often hide WebGL-specific bugs.
Prefer async patterns using WebResourceFetcher + emscripten_async_wget2 for large assets.
Use #ifdef __EMSCRIPTEN__ when behavior needs to differ between native and web builds.
When fixing texture or mipmap issues, prioritize stability with low-resolution textures first before adding high-res streaming.

Recommended Workflow

Make changes in C++
Run ./build-web.sh
Serve locally and test in Chrome
Check browser console and WebGL inspector for texture/mipmap errors
Document any new polyfills or workarounds in SystemModules.h

Future Direction

Robust asynchronous high-resolution texture downloading
Progress indicators for background texture loading
Cleaner LOD architecture
Better texture memory management and caching
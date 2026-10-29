cmake_minimum_required(VERSION 3.18)
project(SolarSystem)

set(CMAKE_CXX_STANDARD 17)

if(EXISTS "${CMAKE_SOURCE_DIR}/external/glm")
    include_directories(${CMAKE_SOURCE_DIR}/external/glm)
endif()

if(EMSCRIPTEN)
    include_directories(${CMAKE_SOURCE_DIR}/external/assimp/include)
    include_directories(${CMAKE_SOURCE_DIR}/external/assimp/build-wasm/include)
endif()

# [PORTING NOTE]
# For Emscripten build:
# 1. Use emcmake cmake -DCMAKE_BUILD_TYPE=Release|Debug ...
# 2. Port/link flags are applied once via target_compile_options / target_link_options below.
# 3. irrKlang and SDL_mixer are not used on web: music is an HTMLAudioElement in web/src/music.ts,
#    and SDL2 / SDL2_image are not linked either (the only SDL_image use is the native window icon).
# 4. Exclude unsupported libs (glew32, OpenGL32, mingw32) in if(EMSCRIPTEN) block.
# See docs/plans/PORTING_GUIDE.md for Wasm exceptions fallback.

if(EMSCRIPTEN)
    add_definitions(-D__EMSCRIPTEN__)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")
endif()

# .cpp only — headers are pulled in through #include, not by being listed here, and
# listing them added nothing but a CMake edit for every new header. Grouped by
# directory to keep this list scannable as the codebase grows.
set(SOLARSYSTEM_SOURCES_CORE
    src/main.cpp
    src/Application.cpp
    src/JsBridge.cpp
    src/WasmExports.cpp
    src/QualitySettings.cpp
    src/ResourceManifest.cpp
    src/PlanetSystemLoader.cpp
    src/CameraController.cpp
    src/TerrainController.cpp
    src/Cinematics.cpp
    src/SceneRenderer.cpp
    src/SceneOverlayRenderer.cpp
    src/StarSystemFactory.cpp
    src/PlatformWindow.cpp
    src/InputHandler.cpp
    src/ObserveMode.cpp
    src/ObserveRenderer.cpp
    src/AudioPlayer.cpp
    src/RenderSettings.cpp
    src/XrSession.cpp
)

set(SOLARSYSTEM_SOURCES_AUX
    # core/: logging, camera, frame timing
    src/core/Log.cpp
    src/core/Camera.cpp
    src/core/CameraNavigation.cpp
    src/core/FrameTimer.cpp
    # gfx/: GL objects and programs
    src/gfx/Shader.cpp
    src/gfx/ShaderLibrary.cpp
    src/gfx/ShaderSource.cpp
    src/gfx/Mesh.cpp
    src/gfx/MeshHolder.cpp
    src/gfx/TextureImage2D.cpp
    src/gfx/GlCapabilities.cpp
    src/gfx/ShadowMapFBO.cpp
    src/gfx/HDR.cpp
    src/gfx/FloatLutTexture.cpp
    src/gfx/FrameUniforms.cpp
    src/gfx/LineStripBatch.cpp
    src/gfx/SurfacePatch.cpp
    src/gfx/SurfacePatchMesh.cpp
    # render/: overlays and post effects built on gfx/
    src/render/LensFlare.cpp
    src/render/TextRenderer.cpp
    src/render/OrbitPathRenderer.cpp
    src/render/MissionPathRenderer.cpp
    src/render/XrPointerRenderer.cpp
    src/render/MagneticFieldLineRenderer.cpp
    src/render/MagneticFieldBloom.cpp
    # render/terrain/: cube-sphere height tiles (GL-free except TerrainRenderer)
    src/render/terrain/CubeSphere.cpp
    src/render/terrain/TerrainTile.cpp
    src/render/terrain/TerrainQuadtree.cpp
    src/render/terrain/TileCache.cpp
    src/render/terrain/TerrainSystem.cpp
    src/render/terrain/TerrainRenderer.cpp
    # assets/: downloads, texture streaming and formats
    src/assets/WebResourceFetcher.cpp
    src/assets/TextureLoadingQueue.cpp
    src/assets/TextureLODController.cpp
    src/assets/PlanetManifestLoader.cpp
    src/assets/TextureFormatSupport.cpp
    src/assets/BlockCompression.cpp
    # astro/: ephemerides, sky events, catalogs, field models (GL-free)
    src/astro/Ephemeris.cpp
    src/astro/SkyEvents.cpp
    src/astro/Observer.cpp
    src/astro/LunarTheory.cpp
    src/astro/StarCatalog.cpp
    src/astro/MissionCatalog.cpp
    src/astro/MagneticFieldParams.cpp
    src/astro/MagneticFieldModel.cpp
    src/astro/MagneticFieldTracer.cpp
)

set(SOLARSYSTEM_SOURCES_SOLAR
    src/Solar_System/SpaceObject.cpp
    src/Solar_System/Planet.cpp
    src/Solar_System/Transformable.cpp
    src/Solar_System/Star.cpp
    src/Solar_System/Sun/Sun.cpp
    src/Solar_System/Satellite.cpp
    src/Solar_System/Atmosphere.cpp
    src/Solar_System/AtmosphereModel.cpp
    src/Solar_System/PlanetaryRing.cpp
    src/Solar_System/SkyBox.cpp
    src/Solar_System/Saturn_System/SaturnRing.cpp
    src/Solar_System/Uranus_System/UranusRing.cpp
    src/Solar_System/OuterShell.cpp
    src/Solar_System/Clouds.cpp
    src/Solar_System/OrbitLayout.cpp
    src/Solar_System/AsteroidOrbit.cpp
    src/Solar_System/AsteroidField.cpp
    src/Solar_System/ObserveSky.cpp
    src/Solar_System/CatalogBody.cpp
    src/Solar_System/CatalogSatellite.cpp
    src/Solar_System/CatalogClouds.cpp
    src/Solar_System/CatalogMaterial.cpp
    src/Solar_System/CatalogMaterialVariants.cpp
)

set(SOLARSYSTEM_SOURCES_3RDPARTY
    src/3rdparty/nv_dds.cpp
    src/3rdparty/ktx2_reader.cpp
)

set(SOLARSYSTEM_SOURCES
    ${SOLARSYSTEM_SOURCES_CORE}
    ${SOLARSYSTEM_SOURCES_AUX}
    ${SOLARSYSTEM_SOURCES_SOLAR}
    ${SOLARSYSTEM_SOURCES_3RDPARTY}
)

if(WIN32)
    list(APPEND SOLARSYSTEM_SOURCES resource/resource.rc)
endif()

add_executable(${PROJECT_NAME} ${SOLARSYSTEM_SOURCES})

# Copying of all necessary dll files on which the executable depends (Windows only)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/lib/3rdparty"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endif()

if(EMSCRIPTEN)
    # Port settings that genuinely affect compilation (they put the port's headers on
    # the include path) — applied once to compile and once to link, no duplicates.
    # Anything emcc reports as "linker setting ignored during compilation" belongs in
    # SOLAR_EM_LINK_FLAGS instead, or -Wall drowns in -Wunused-command-line-argument.
    set(SOLAR_EM_PORT_FLAGS
        "SHELL:-sUSE_FREETYPE=1"
        # No ASYNCIFY / JSPI: every download goes through the callback-based
        # emscripten_async_wget2 path in WebResourceFetcher::DownloadFile, and the
        # scene is not constructed until those callbacks have settled, so nothing
        # suspends the Wasm stack. See docs/plans/PORTING_GUIDE.md §3b.
        #
        # With ASYNCIFY gone, native Wasm exceptions are usable (Chrome 95+,
        # Firefox 100+, Safari 15.2+). external/assimp must be built with the same
        # flag — setup_web_dependencies.sh passes it.
        "SHELL:-fwasm-exceptions"
    )

    # Codegen: WebAssembly SIMD (Chrome 91+, Firefox 89+, Safari 16.4+). It has to be on
    # the compile and the link line, or LTO drops the vector ops. The users are the CPU DXT
    # decode (BlockCompression.cpp), the magnetic-field tracer and the belt update.
    set(SOLAR_EM_CODEGEN_FLAGS -msimd128)

    # Compile-only: warnings would just be dead arguments on the link line.
    # Codegen: WebAssembly SIMD (Chrome 91+, Firefox 89+, Safari 16.4+). It has to be on
    # the compile and the link line, or LTO drops the vector ops. The users are the CPU DXT
    # decode (BlockCompression.cpp), the magnetic-field tracer and the belt update.
    set(SOLAR_EM_CODEGEN_FLAGS -msimd128)

    set(SOLAR_EM_WARNING_FLAGS -Wall -Wextra)

    # Link-only: memory, preloads, module shape, JS interop.
    # textures_low (~408 KB) preloaded so planets stay visible without a live asset server.
    # Preload only what the page samples or parses: resource/icons (14 MB) is read by the
    # native window icon alone, so it stays out of SolarSystem.data.
    # Sounds are LFS-excluded — do not preload (CI/sandbox).
    set(SOLAR_EM_LINK_FLAGS
        "SHELL:-sUSE_GLFW=3"
        "SHELL:-sFULL_ES3=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
        "SHELL:-sGL_ENABLE_GET_PROC_ADDRESS=1"
        # This target is only ever a page: no Node / shell / worker branches in the glue.
        "SHELL:-sENVIRONMENT=web"
        # The default 64 KB stack is small for recursive JSON parsing and Assimp.
        "SHELL:-sSTACK_SIZE=1MB"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        # This target is only ever a page: no Node / shell / worker branches in the glue.
        "SHELL:-sENVIRONMENT=web"
        # The default 64 KB stack is small for recursive JSON parsing and Assimp.
        "SHELL:-sSTACK_SIZE=1MB"
        # Explicit default: fixed-buffer growth (new ArrayBuffer + view refresh on
        # grow), not resizable-ArrayBuffer growth. Current Chrome WebGL/DOM APIs
        # reject views backed by a resizable buffer (see web/src/bootstrap.ts history).
        "SHELL:-sGROWABLE_ARRAYBUFFERS=0"
        "SHELL:-sINITIAL_MEMORY=256MB"
        "SHELL:-sMAXIMUM_MEMORY=1GB"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/shaders@/resource/shaders"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/planet_manifest.json@/resource/planet_manifest.json"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/missions@/resource/missions"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/sky@/resource/sky"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/fonts@/resource/fonts"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/textures_low@/resource/textures_low"
        # Baked atmosphere LUTs (~1.1 MB, 8 bodies): tools/atmosphere_lut_baker, ARCHITECTURE §9.2.
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/atmosphere@/resource/atmosphere"
        "SHELL:-sMODULARIZE=1"
        "SHELL:-sEXPORT_ES6=1"
        # 'GL' is needed so web/src/wasmBridge.ts can register the XRWebGLLayer's opaque
        # framebuffer in Emscripten's GL name table; without a name the WASM side cannot
        # rebind it after an off-screen pass (see SceneRenderer.cpp ShadowMapPass).
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPF32','GL','UTF8ToString','FS']"
        # Default list (see emscripten/src/settings.js INCOMING_MODULE_JS_API) plus
        # 'preinitializedWebGLContext' — web/src/bootstrap.ts creates the WebGL2 context
        # itself (see web/src/webglContext.ts) so GLFW's context creation can reuse it
        # instead of building its own attributes from GLFW window hints alone.
        "SHELL:-sINCOMING_MODULE_JS_API=['ENVIRONMENT','arguments','canvas','dynamicLibraries','elementPointerLock','instantiateWasm','locateFile','monitorRunDependencies','noExitRuntime','noInitialRun','onAbort','onExit','onRuntimeInitialized','postRun','preInit','preRun','print','printErr','setStatus','statusMessage','stderr','stdin','stdout','thisProgram','wasm','websocket','preinitializedWebGLContext']"
        # Generated by scripts/generate-wasm-exports.mjs (checked via
        # `npm run generate:wasm-exports:check` in CI) from the
        # EMSCRIPTEN_KEEPALIVE exports in WasmExports.cpp, plus '_main'.
        "SHELL:-sEXPORTED_FUNCTIONS=@${CMAKE_SOURCE_DIR}/scripts/wasm-exports.json"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(SOLAR_EM_COMPILE_OPT_FLAGS -O0 -g)
        # GL_ASSERTIONS validates GL enum/handle arguments in the JS GL layer.
        # Add "SHELL:-sGL_DEBUG=1" temporarily to also trace every GL call.
        # -gsource-map: DevTools shows C++ sources and names in Wasm stacks.
        set(SOLAR_EM_LINK_OPT_FLAGS -O0 -g -gsource-map "SHELL:-sASSERTIONS=1" "SHELL:-sGL_ASSERTIONS=1")
    else()
        # Release (default) and RelWithDebInfo / empty CMAKE_BUILD_TYPE.
        # --closure 1 halves the JS glue (183 380 B -> 87 678 B). The old blocker was
        # Closure renaming the Module.foo / window.__solarSystemFoo properties read from
        # EM_ASM; those are all bracket-notation now (JsBridge.cpp, QualitySettings.cpp,
        # WebResourceFetcher.cpp), which Closure never renames, so no externs file is
        # needed. New JS bridges MUST use Module['name'], not Module.name -- a dotted
        # read is silently renamed and the callback simply never fires in Release.
        # See the "--closure 1" section of docs/plans/PORTING_GUIDE.md §3b.
        set(SOLAR_EM_COMPILE_OPT_FLAGS -O3 -flto)
        # --emit-symbol-map writes SolarSystem.js.symbols (minified name -> original), so a
        # Closure-renamed stack can still be read. Keep --closure 1 on.
        set(SOLAR_EM_LINK_OPT_FLAGS -O3 -flto "SHELL:--closure 1" "SHELL:--emit-symbol-map")
    endif()

    target_compile_options(${PROJECT_NAME} PRIVATE
        ${SOLAR_EM_PORT_FLAGS}
        ${SOLAR_EM_CODEGEN_FLAGS}
        ${SOLAR_EM_WARNING_FLAGS}
        ${SOLAR_EM_CODEGEN_FLAGS}
        ${SOLAR_EM_COMPILE_OPT_FLAGS}
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        ${SOLAR_EM_PORT_FLAGS}
        ${SOLAR_EM_CODEGEN_FLAGS}
        ${SOLAR_EM_LINK_FLAGS}
        ${SOLAR_EM_CODEGEN_FLAGS}
        ${SOLAR_EM_LINK_OPT_FLAGS}
    )

    # --preload-file paths and the export list are link *options*, not inputs, so make does
    # not see them change. Declare them as link dependencies: editing a shader, a font, a
    # mission JSON or scripts/wasm-exports.json then relinks and rebuilds SolarSystem.data.
    # CONFIGURE_DEPENDS re-globs at build time, so a file added to a preloaded directory
    # is picked up too.
    set(SOLAR_EM_PRELOAD_DIRS shaders missions sky fonts textures_low atmosphere)
    set(SOLAR_EM_LINK_INPUTS
        ${CMAKE_SOURCE_DIR}/scripts/wasm-exports.json
        ${CMAKE_SOURCE_DIR}/resource/planet_manifest.json)
    foreach(preloadDir IN LISTS SOLAR_EM_PRELOAD_DIRS)
        file(GLOB_RECURSE preloadFiles CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resource/${preloadDir}/*")
        list(APPEND SOLAR_EM_LINK_INPUTS ${preloadFiles})
    endforeach()
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS ${SOLAR_EM_LINK_INPUTS})

    # Emscripten provides GLFW and FreeType via ports
    target_link_libraries(${PROJECT_NAME}
        ${CMAKE_SOURCE_DIR}/external/assimp/build-wasm/lib/libassimp.a
        z
    )
elseif(WIN32)
    target_link_libraries(${PROJECT_NAME}
        glfw3
        glew32
        OpenGL32
        mingw32
        SDL2
        SDL2_image
        libassimp
        libfreetype-6
        irrKlang
    )
else()
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(glfw3 REQUIRED)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(assimp REQUIRED)

    target_compile_definitions(${PROJECT_NAME} PRIVATE SOLARSYSTEM_USE_SDL_MIXER)

    target_link_libraries(${PROJECT_NAME}
        OpenGL::GL
        GLEW::GLEW
        glfw
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        Freetype::Freetype
        assimp::assimp
    )
endif()

if(NOT EMSCRIPTEN AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

option(SOLARSYSTEM_BUILD_TESTS "Build native unit tests" OFF)
option(SOLARSYSTEM_BUILD_TOOLS "Build offline asset tools (atmosphere LUT baker)" OFF)
if(NOT EMSCRIPTEN AND (SOLARSYSTEM_BUILD_TOOLS OR SOLARSYSTEM_BUILD_TESTS))
    add_subdirectory(tools/atmosphere_lut_baker)
endif()
if(SOLARSYSTEM_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
    # The committed resource/atmosphere/*.ktx2 must match the catalog (see the baker).
    add_test(NAME AtmosphereLutsFresh
             COMMAND atmosphere_lut_baker --check --out ${CMAKE_SOURCE_DIR}/resource/atmosphere)
endif()

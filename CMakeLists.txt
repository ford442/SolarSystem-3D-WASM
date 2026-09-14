cmake_minimum_required(VERSION 3.18)
project(SolarSystem)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_SOURCE_DIR}/external/glm)

if(EMSCRIPTEN)
    include_directories(${CMAKE_SOURCE_DIR}/external/assimp/include)
    include_directories(${CMAKE_SOURCE_DIR}/external/assimp/build-wasm/include)
endif()

# [PORTING NOTE]
# For Emscripten build:
# 1. Use emcmake cmake -DCMAKE_BUILD_TYPE=Release|Debug ...
# 2. Port/link flags are applied once via target_compile_options / target_link_options below.
# 3. irrKlang is NOT supported. Replace with SDL_mixer.
# 4. Exclude unsupported libs (glew32, OpenGL32, mingw32) in if(EMSCRIPTEN) block.
# See docs/plans/PORTING_GUIDE.md for Wasm exceptions fallback.

if(EMSCRIPTEN)
    add_definitions(-D__EMSCRIPTEN__)
    add_definitions(-DSOLARSYSTEM_USE_SDL_MIXER)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")
endif()

set(SOLARSYSTEM_SOURCES
    src/main.cpp
    src/Auxiliary_Modules/WebResourceFetcher.cpp
    src/Auxiliary_Modules/WebResourceFetcher.h
    src/Auxiliary_Modules/Shader.cpp
    src/Auxiliary_Modules/Shader.h
    src/Auxiliary_Modules/Mesh.cpp
    src/Auxiliary_Modules/Mesh.h
    src/Auxiliary_Modules/MeshHolder.cpp
    src/Auxiliary_Modules/MeshHolder.h
    src/Auxiliary_Modules/Camera.cpp
    src/Auxiliary_Modules/Camera.h
    src/Auxiliary_Modules/FPS_Handler.cpp
    src/Auxiliary_Modules/FPS_Handler.h
    src/Solar_System/SpaceObject.cpp
    src/Solar_System/SpaceObject.h
    src/Solar_System/Planet.cpp
    src/Solar_System/Planet.h
    src/Solar_System/Transformable.cpp
    src/Solar_System/Transformable.h
    src/Application.cpp
    src/Application.h
    src/ApplicationTypes.h
    src/JsBridge.cpp
    src/JsBridge.h
    src/WasmExports.cpp
    src/WasmExports.h
    src/QualitySettings.cpp
    src/QualitySettings.h
    src/ResourceManifest.cpp
    src/ResourceManifest.h
    src/PlanetSystemLoader.cpp
    src/SceneRenderer.cpp
    src/SceneOverlayRenderer.cpp
    src/StarSystemFactory.cpp
    src/PlatformWindow.cpp
    src/InputHandler.cpp
    src/AudioPlayer.cpp
    src/RenderSettings.cpp
    src/XrSession.cpp
    src/SimState.h
    src/SimState.cpp
    src/Auxiliary_Modules/TextureImage2D.cpp
    src/Auxiliary_Modules/TextureImage2D.h
    src/Auxiliary_Modules/TextureLoadingQueue.cpp
    src/Auxiliary_Modules/TextureLoadingQueue.h
    src/Auxiliary_Modules/GlCapabilities.cpp
    src/Auxiliary_Modules/GlCapabilities.h
    src/Solar_System/Star.cpp
    src/Solar_System/Star.h
    src/Solar_System/Sun/Sun.cpp
    src/Solar_System/Sun/Sun.h
    src/Solar_System/Satellite.cpp
    src/Solar_System/Satellite.h
    src/3rdparty/nv_dds.cpp
    src/3rdparty/nv_dds.h
    src/Auxiliary_Modules/ShadowMapFBO.cpp
    src/Auxiliary_Modules/ShadowMapFBO.h
    src/Solar_System/Atmosphere.cpp
    src/Solar_System/Atmosphere.h
    src/Auxiliary_Modules/LensFlare.cpp
    src/Auxiliary_Modules/LensFlare.h
    src/Auxiliary_Modules/OrbitPathRenderer.cpp
    src/Auxiliary_Modules/OrbitPathRenderer.h
    src/Auxiliary_Modules/MagneticFieldParams.cpp
    src/Auxiliary_Modules/MagneticFieldParams.h
    src/Auxiliary_Modules/MagneticFieldModel.cpp
    src/Auxiliary_Modules/MagneticFieldModel.h
    src/Auxiliary_Modules/MagneticFieldTracer.cpp
    src/Auxiliary_Modules/MagneticFieldTracer.h
    src/Auxiliary_Modules/MagneticFieldLineMesh.cpp
    src/Auxiliary_Modules/MagneticFieldLineMesh.h
    src/Auxiliary_Modules/MagneticFieldLineRenderer.cpp
    src/Auxiliary_Modules/MagneticFieldLineRenderer.h
    src/Auxiliary_Modules/MagneticFieldBloom.cpp
    src/Auxiliary_Modules/MagneticFieldBloom.h
    src/Auxiliary_Modules/Ephemeris.cpp
    src/Auxiliary_Modules/Ephemeris.h
    src/Auxiliary_Modules/TextRenderer.cpp
    src/Auxiliary_Modules/TextRenderer.h
    src/Solar_System/PlanetaryRing.cpp
    src/Solar_System/PlanetaryRing.h
    src/Solar_System/SkyBox.cpp
    src/Solar_System/SkyBox.h
    src/Solar_System/Saturn_System/SaturnRing.cpp
    src/Solar_System/Saturn_System/SaturnRing.h
    src/Solar_System/Uranus_System/UranusRing.cpp
    src/Solar_System/Uranus_System/UranusRing.h
    src/Solar_System/SolarSystem.h
    src/Solar_System/OuterShell.cpp
    src/Solar_System/OuterShell.h
    src/Solar_System/Clouds.cpp
    src/Solar_System/Clouds.h
    src/Auxiliary_Modules/HDR.cpp
    src/Auxiliary_Modules/HDR.h
    src/Auxiliary_Modules/AuxiliaryModules.h
    src/SystemModules.h
    src/Solar_System/OrbitLayout.cpp
    src/Solar_System/OrbitLayout.h
    src/Solar_System/AsteroidOrbit.cpp
    src/Solar_System/AsteroidOrbit.h
    src/Solar_System/AsteroidField.cpp
    src/Solar_System/AsteroidField.h
    src/Solar_System/CatalogBody.cpp
    src/Solar_System/CatalogBody.h
    src/Solar_System/CatalogSatellite.cpp
    src/Solar_System/CatalogSatellite.h
    src/Solar_System/CatalogClouds.cpp
    src/Solar_System/CatalogClouds.h
    src/Solar_System/SystemVisuals.h
    src/Solar_System/BodyCatalog.generated.h
    src/Auxiliary_Modules/SkyEvents.cpp
    src/Auxiliary_Modules/SkyEvents.h
    src/XrState.h
)

if(WIN32)
    list(APPEND SOLARSYSTEM_SOURCES resource/resource.rc)
endif()

add_executable(${PROJECT_NAME} ${SOLARSYSTEM_SOURCES})

target_sources(${PROJECT_NAME} PRIVATE
    src/Auxiliary_Modules/TextureLODController.cpp
    src/Auxiliary_Modules/TextureLODController.h
    src/Auxiliary_Modules/PlanetManifestLoader.cpp
    src/Auxiliary_Modules/PlanetManifestLoader.h
    src/PlanetSystemManifest.h
    src/Solar_System/TexturePaths.h
)

# Copying of all necessary dll files on which the executable depends (Windows only)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/lib/3rdparty"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endif()

if(EMSCRIPTEN)
    # Port / shared settings — applied once to compile and once to link (no duplicates).
    set(SOLAR_EM_PORT_FLAGS
        "SHELL:-sUSE_GLFW=3"
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_SDL_IMAGE=2"
        "SHELL:-sSDL2_IMAGE_FORMATS=['png','jpg']"
        "SHELL:-sUSE_SDL_MIXER=2"
        "SHELL:-sUSE_FREETYPE=1"
        "SHELL:-sFULL_ES3=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        # Explicit default: fixed-buffer growth (new ArrayBuffer + view refresh on
        # grow), not resizable-ArrayBuffer growth. Current Chrome WebGL/DOM APIs
        # reject views backed by a resizable buffer (see web/src/bootstrap.ts history).
        "SHELL:-sGROWABLE_ARRAYBUFFERS=0"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
        "SHELL:-sGL_ENABLE_GET_PROC_ADDRESS=1"
        # ASYNCIFY required for WebResourceFetcher (emscripten_wget_data).
        # Native -fwasm-exceptions is incompatible with ASYNCIFY=1 (emcc warning);
        # keep the JS exception path so try/catch still works.
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
    )

    # Link-only: memory, preloads, module shape, JS interop.
    # textures_low (~124 KB) preloaded so planets stay visible without a live asset server.
    # Sounds are LFS-excluded — do not preload (CI/sandbox).
    set(SOLAR_EM_LINK_FLAGS
        "SHELL:-sINITIAL_MEMORY=256MB"
        "SHELL:-sMAXIMUM_MEMORY=1GB"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/shaders@/resource/shaders"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/planet_manifest.json@/resource/planet_manifest.json"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/fonts@/resource/fonts"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/icons@/resource/icons"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/resource/textures_low@/resource/textures_low"
        "SHELL:-sMODULARIZE=1"
        "SHELL:-sEXPORT_ES6=1"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPF32']"
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
        set(SOLAR_EM_LINK_OPT_FLAGS -O0 -g "SHELL:-sASSERTIONS=1")
    else()
        # Release (default) and RelWithDebInfo / empty CMAKE_BUILD_TYPE.
        # Do not enable --closure 1: it strips EM_ASM tables this app needs for
        # window.updateLoadingProgress / mobile detection / similar bridges.
        set(SOLAR_EM_COMPILE_OPT_FLAGS -O3 -flto)
        set(SOLAR_EM_LINK_OPT_FLAGS -O3 -flto)
    endif()

    target_compile_options(${PROJECT_NAME} PRIVATE
        ${SOLAR_EM_PORT_FLAGS}
        ${SOLAR_EM_COMPILE_OPT_FLAGS}
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        ${SOLAR_EM_PORT_FLAGS}
        ${SOLAR_EM_LINK_FLAGS}
        ${SOLAR_EM_LINK_OPT_FLAGS}
    )

    # Emscripten provides GLFW, SDL2, SDL_image, SDL_mixer, and FreeType via ports
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

option(SOLARSYSTEM_BUILD_TESTS "Build native unit tests" OFF)
if(SOLARSYSTEM_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()

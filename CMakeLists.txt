cmake_minimum_required(VERSION 3.18)
project(SolarSystem)

set(CMAKE_CXX_STANDARD 17)

# --- FIX 1: Add Include Paths for Dependencies ---
# Using CMAKE_SOURCE_DIR ensures paths are correct relative to the root
include_directories(${CMAKE_SOURCE_DIR}/external/glm)
include_directories(${CMAKE_SOURCE_DIR}/external/assimp/include)
include_directories(${CMAKE_SOURCE_DIR}/external/assimp/build-wasm/include)

# [PORTING NOTE]
# For Emscripten build:
# 1. Use emcmake cmake ...
# 2. Add dependencies like: -s USE_GLFW=3 -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s USE_FREETYPE=1
# 3. irrKlang is NOT supported. Replace with SDL_mixer.
# 4. Exclude unsupported libs (glew32, OpenGL32, mingw32) in if(EMSCRIPTEN) block.

if(EMSCRIPTEN)
    add_definitions(-D__EMSCRIPTEN__)
    
    # Emscripten compiler flags for dependencies and WebGL 2 support
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_GLFW=3 -s USE_SDL=2 -s USE_SDL_IMAGE=2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL_MIXER=2 -s USE_FREETYPE=1")
    
    # --- FIX 2: Removed WEBGL2_BACKEND=1 (Deprecated/Error) ---
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s FULL_ES3=1")
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s MAX_WEBGL_VERSION=2 -s MIN_WEBGL_VERSION=2")
    
    # Enable compressed texture support for DDS files
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s GL_ENABLE_GET_PROC_ADDRESS=1")
    
    # Linker flags for preloading assets and output configuration
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --preload-file ${CMAKE_SOURCE_DIR}/resource@/resource")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s EXPORTED_FUNCTIONS=['_main']")
    
    # Output .html file with canvas
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

add_executable(${PROJECT_NAME} resource/resource.rc src/main.cpp src/Auxiliary_Modules/Shader.cpp src/Auxiliary_Modules/Shader.h src/Auxiliary_Modules/Mesh.cpp src/Auxiliary_Modules/Mesh.h src/Auxiliary_Modules/MeshHolder.cpp src/Auxiliary_Modules/MeshHolder.h src/Auxiliary_Modules/Camera.cpp src/Auxiliary_Modules/Camera.h src/Auxiliary_Modules/FPS_Handler.cpp src/Auxiliary_Modules/FPS_Handler.h src/Solar_System/SpaceObject.cpp src/Solar_System/SpaceObject.h src/Solar_System/Planet.cpp src/Solar_System/Planet.h src/Solar_System/Earth_System/Earth.cpp src/Solar_System/Earth_System/Earth.h src/Solar_System/Transformable.cpp src/Solar_System/Transformable.h src/Application.cpp src/Application.h src/Auxiliary_Modules/TextureImage2D.cpp src/Auxiliary_Modules/TextureImage2D.h src/Solar_System/Star.cpp src/Solar_System/Star.h src/Solar_System/Sun/Sun.cpp src/Solar_System/Sun/Sun.h src/Solar_System/Satellite.cpp src/Solar_System/Satellite.h src/Solar_System/Earth_System/Moon.cpp src/Solar_System/Earth_System/Moon.h src/3rdparty/nv_dds.cpp src/3rdparty/nv_dds.h src/Auxiliary_Modules/ShadowMapFBO.cpp src/Auxiliary_Modules/ShadowMapFBO.h src/Solar_System/Atmosphere.cpp src/Solar_System/Atmosphere.h src/Auxiliary_Modules/LensFlare.cpp src/Auxiliary_Modules/LensFlare.h src/Solar_System/Mercury/Mercury.cpp src/Solar_System/Mercury/Mercury.h src/Solar_System/Venus/Venus.cpp src/Solar_System/Venus/Venus.h src/Solar_System/Mars_System/Mars.cpp src/Solar_System/Mars_System/Mars.h src/Solar_System/Mars_System/Phobos.cpp src/Solar_System/Mars_System/Phobos.h src/Solar_System/Mars_System/Deimos.cpp src/Solar_System/Mars_System/Deimos.h src/Auxiliary_Modules/TextRenderer.cpp src/Auxiliary_Modules/TextRenderer.h src/Solar_System/Pluto_System/Pluto.cpp src/Solar_System/Pluto_System/Pluto.h src/Solar_System/Pluto_System/Charon.cpp src/Solar_System/Pluto_System/Charon.h src/Solar_System/Neptune_System/Neptune.cpp src/Solar_System/Neptune_System/Neptune.h src/Solar_System/Neptune_System/Triton.cpp src/Solar_System/Neptune_System/Triton.h src/Solar_System/PlanetaryRing.cpp src/Solar_System/PlanetaryRing.h src/Solar_System/Uranus_System/Uranus.cpp src/Solar_System/Uranus_System/Uranus.h src/Solar_System/Uranus_System/Ariel.cpp src/Solar_System/Uranus_System/Ariel.h src/Solar_System/Uranus_System/Miranda.cpp src/Solar_System/Uranus_System/Miranda.h src/Solar_System/Uranus_System/Umbriel.cpp src/Solar_System/Uranus_System/Umbriel.h src/Solar_System/Uranus_System/Titania.cpp src/Solar_System/Uranus_System/Titania.h src/Solar_System/Uranus_System/Oberon.cpp src/Solar_System/Uranus_System/Oberon.h src/Solar_System/Saturn_System/Saturn.cpp src/Solar_System/Saturn_System/Saturn.h src/Solar_System/Saturn_System/Mimas.cpp src/Solar_System/Saturn_System/Mimas.h src/Solar_System/Saturn_System/Enceladus.cpp src/Solar_System/Saturn_System/Enceladus.h src/Solar_System/Saturn_System/Tethys.cpp src/Solar_System/Saturn_System/Tethys.h src/Solar_System/Saturn_System/Dione.cpp src/Solar_System/Saturn_System/Dione.h src/Solar_System/Saturn_System/Rhea.cpp src/Solar_System/Saturn_System/Rhea.h src/Solar_System/Saturn_System/Titan.cpp src/Solar_System/Saturn_System/Titan.h src/Solar_System/Saturn_System/Iapetus.cpp src/Solar_System/Saturn_System/Iapetus.h src/Solar_System/Jupiter_System/Jupiter.cpp src/Solar_System/Jupiter_System/Jupiter.h src/Solar_System/Jupiter_System/Io.cpp src/Solar_System/Jupiter_System/Io.h src/Solar_System/Jupiter_System/Europa.cpp src/Solar_System/Jupiter_System/Europa.h src/Solar_System/Jupiter_System/Ganymede.cpp src/Solar_System/Jupiter_System/Ganymede.h src/Solar_System/Jupiter_System/Callisto.cpp src/Solar_System/Jupiter_System/Callisto.h src/Solar_System/SkyBox.cpp src/Solar_System/SkyBox.h src/Solar_System/Saturn_System/SaturnRing.cpp src/Solar_System/Saturn_System/SaturnRing.h src/Solar_System/Uranus_System/UranusRing.cpp src/Solar_System/Uranus_System/UranusRing.h src/Solar_System/SolarSystem.h src/Solar_System/OuterShell.cpp src/Solar_System/OuterShell.h src/Solar_System/Clouds.cpp src/Solar_System/Clouds.h src/Auxiliary_Modules/HDR.cpp src/Auxiliary_Modules/HDR.h src/Auxiliary_Modules/AuxiliaryModules.h src/SystemModules.h src/Solar_System/Uranus_System/UranusClouds.cpp src/Solar_System/Uranus_System/UranusClouds.h src/Solar_System/Neptune_System/NeptuneClouds.cpp src/Solar_System/Neptune_System/NeptuneClouds.h src/Solar_System/Earth_System/EarthClouds.cpp src/Solar_System/Earth_System/EarthClouds.h)

# Copying of all necessary dll files on which the executable depends
if(NOT EMSCRIPTEN)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/lib/3rdparty"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endif()

if(EMSCRIPTEN)
    # Emscripten provides GLFW, SDL2, SDL_image, SDL_mixer, and FreeType via ports
    # Link assimp (needs to be built separately or available)
    target_link_libraries(${PROJECT_NAME}
        # --- FIX 3: Link Static Assimp Library & Zlib ---
        ${CMAKE_SOURCE_DIR}/external/assimp/build-wasm/lib/libassimp.a
        z
    )
else()
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
endif()

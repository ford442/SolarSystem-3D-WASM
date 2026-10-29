include(FetchContent)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

set(SOLARSYSTEM_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/Solar_System/OrbitLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/Solar_System/AsteroidOrbit.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/Ephemeris.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/SkyEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/Observer.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/LunarTheory.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/StarCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MagneticFieldParams.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MagneticFieldModel.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MagneticFieldTracer.cpp
    ${CMAKE_SOURCE_DIR}/src/QualitySettings.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/PlanetManifestLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MissionCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureLoadingQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureLODController.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/WebResourceFetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureFormatSupport.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/BlockCompression.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/ktx2_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/ShaderSource.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Camera.cpp
    ${CMAKE_SOURCE_DIR}/src/core/CameraNavigation.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/SurfacePatch.cpp
    ${CMAKE_SOURCE_DIR}/src/Solar_System/CatalogMaterialVariants.cpp
    ${CMAKE_SOURCE_DIR}/src/Solar_System/AtmosphereModel.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/ktx2_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/CubeSphere.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/TerrainTile.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/TerrainQuadtree.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/TileCache.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/TerrainSystem.cpp
)

add_executable(SolarSystemTests
    test_orbit_layout.cpp
    test_render_origin.cpp
    test_camera_navigation.cpp
    test_mesh_anchor.cpp
    test_surface_patch.cpp
    test_terrain_quadtree.cpp
    test_ephemeris.cpp
    test_quality_settings.cpp
    test_planet_manifest_loader.cpp
    test_texture_loading_queue.cpp
    test_texture_lod_controller.cpp
    test_web_resource_fetcher.cpp
    test_asteroid_field.cpp
    test_magnetic_field.cpp
    test_sky_events.cpp
    test_observer.cpp
    test_lunar_theory.cpp
    test_seated_view.cpp
    test_star_catalog.cpp
    test_satellite_orbit.cpp
    test_eclipse_caster.cpp
    test_mission_catalog.cpp
    test_body_catalog.cpp
    test_texture_formats.cpp
    test_shader_source.cpp
    test_atmosphere_model.cpp
    test_giant_atmospheres.cpp
    test_frame_passes.cpp
    test_frustum.cpp
    test_shadow_fit.cpp
    test_log.cpp
    stub_texture_image2d.cpp
    ${SOLARSYSTEM_TEST_SOURCES}
)

target_compile_definitions(SolarSystemTests PRIVATE SOLARSYSTEM_BUILD_TESTS
    SOLARSYSTEM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(SolarSystemTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
if(EXISTS "${CMAKE_SOURCE_DIR}/external/glm")
    target_include_directories(SolarSystemTests PRIVATE ${CMAKE_SOURCE_DIR}/external/glm)
endif()
# GL-owning types (TextureImage2D, LineStripBatch) hold gfx/GlHandle members whose inline
# destructors reference glDelete*, so the test binary links GL even though these tests never
# create a context: a default-constructed handle is empty and deletes nothing.
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
target_link_libraries(SolarSystemTests PRIVATE GTest::gtest_main OpenGL::GL GLEW::GLEW)

include(GoogleTest)
gtest_discover_tests(SolarSystemTests)

# GL-touching tests: a hidden GLFW window with a real GL 4.6 core context, so handles,
# program linking, sampler units, the uniform cache, framebuffers and one draw run against a
# driver. CI runs them under Xvfb/llvmpipe; with no display every test reports SKIPPED.
find_package(glfw3 REQUIRED)
add_executable(SolarSystemGlTests
    gl/test_gl.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/Shader.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/ShaderSource.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/GlCapabilities.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/FrameUniforms.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/LineStripBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/ShadowMapFBO.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/HDR.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/TextureImage2D.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/WebResourceFetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureFormatSupport.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/BlockCompression.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/nv_dds.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/ktx2_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/Solar_System/CatalogMaterialVariants.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/CubeSphere.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/TerrainTile.cpp
    ${CMAKE_SOURCE_DIR}/src/render/terrain/TerrainRenderer.cpp
)
target_compile_definitions(SolarSystemGlTests PRIVATE SOLARSYSTEM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(SolarSystemGlTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(EXISTS "${CMAKE_SOURCE_DIR}/external/glm")
    target_include_directories(SolarSystemGlTests PRIVATE ${CMAKE_SOURCE_DIR}/external/glm)
endif()
target_link_libraries(SolarSystemGlTests PRIVATE GTest::gtest_main OpenGL::GL GLEW::GLEW glfw)
gtest_discover_tests(SolarSystemGlTests TEST_PREFIX "gl." DISCOVERY_MODE PRE_TEST)

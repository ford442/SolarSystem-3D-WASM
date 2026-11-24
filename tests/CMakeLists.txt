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
    ${CMAKE_SOURCE_DIR}/src/Solar_System/StarSystemCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Json.cpp
    ${CMAKE_SOURCE_DIR}/src/Solar_System/AsteroidOrbit.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/Ephemeris.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/SkyEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/Observer.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/LunarTheory.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/StarCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MagneticFieldParams.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MagneticFieldModel.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MagneticFieldTracer.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/SpaceWeather.cpp
    ${CMAKE_SOURCE_DIR}/src/QualitySettings.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/PlanetManifestLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/MissionCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/FlightConstants.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/StateVector.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/Lambert.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/Porkchop.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/Propagator.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/FlightPlan.cpp
    ${CMAKE_SOURCE_DIR}/src/astro/flight/Spacecraft.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureLoadingQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureLODController.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/WebResourceFetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureFormatSupport.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/BlockCompression.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureDecode.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/UploadQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/ModelCache.cpp
    ${CMAKE_SOURCE_DIR}/src/core/FrameProfile.cpp
    ${CMAKE_SOURCE_DIR}/src/render/graph/FrameGraph.cpp
    ${CMAKE_SOURCE_DIR}/src/render/graph/FramePlan.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/rhi/Backend.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/rhi/PipelineTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/rhi/RenderPassPlanner.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/rhi/TimestampPairs.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/ktx2_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/nv_dds.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/ShaderSource.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/JobSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Camera.cpp
    ${CMAKE_SOURCE_DIR}/src/core/CameraNavigation.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/SurfacePatch.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/GpuResourceRegistry.cpp
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
    test_space_weather.cpp
    test_sky_events.cpp
    test_observer.cpp
    test_lunar_theory.cpp
    test_seated_view.cpp
    test_star_catalog.cpp
    test_satellite_orbit.cpp
    test_eclipse_caster.cpp
    test_mission_catalog.cpp
    test_flight.cpp
    test_body_catalog.cpp
    test_star_physics.cpp
    test_star_systems.cpp
    test_json.cpp
    test_texture_formats.cpp
    test_shader_source.cpp
    test_atmosphere_model.cpp
    test_giant_atmospheres.cpp
    test_frame_graph.cpp
    test_frame_plan.cpp
    test_rhi_webgpu_model.cpp
    test_frustum.cpp
    test_shadow_fit.cpp
    test_log.cpp
    test_job_system.cpp
    test_texture_decode.cpp
    test_upload_queue.cpp
    test_model_cache.cpp
    test_frame_profile.cpp
    test_gpu_resource_registry.cpp
    test_model_cache.cpp
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
find_package(Threads REQUIRED)
find_package(assimp REQUIRED)
target_link_libraries(SolarSystemTests PRIVATE GTest::gtest_main OpenGL::GL GLEW::GLEW Threads::Threads assimp::assimp)

include(GoogleTest)
gtest_discover_tests(SolarSystemTests)

# GL-touching tests: a hidden GLFW window with a real GL 4.6 core context, so handles,
# program linking, sampler units, the uniform cache, framebuffers and one draw run against a
# driver. CI runs them under Xvfb/llvmpipe; with no display every test reports SKIPPED.
find_package(glfw3 REQUIRED)
add_executable(SolarSystemGlTests
    gl/test_gl.cpp
    gl/test_gpu_timer.cpp
    gl/test_solar_wind.cpp
    gl/test_context_restore.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/Mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/GpuTimer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/JobSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/Shader.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/ShaderSource.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/GlCapabilities.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/FrameUniforms.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/GpuResourceRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/SpaceWeatherUniforms.cpp
    ${CMAKE_SOURCE_DIR}/src/render/SolarWindRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/LineStripBatch.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/ShadowMapFBO.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/HDR.cpp
    ${CMAKE_SOURCE_DIR}/src/gfx/TextureImage2D.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/WebResourceFetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureFormatSupport.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/BlockCompression.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/TextureDecode.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/UploadQueue.cpp
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

# The WebGPU backend against a real device (docs/ARCHITECTURE.md §7.9): Dawn on whatever Vulkan
# adapter there is (CI: SwiftShader or lavapipe; set VK_ICD_FILENAMES to pick one), plus a hidden
# GL context for the GL-vs-WebGPU parity tests. No adapter: every test skips.
if(SOLARSYSTEM_WEBGPU)
    add_executable(SolarSystemWgpuTests
        wgpu/test_wgpu.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/PipelineTracker.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/RenderPassPlanner.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/TimestampPairs.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/wgpu/WgpuDevice.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/wgpu/WgpuProgram.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/wgpu/WgpuCommandList.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/rhi/wgpu/WgpuSelfTest.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/Shader.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/ShaderSource.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/GlCapabilities.cpp
        ${CMAKE_SOURCE_DIR}/src/assets/TextureFormatSupport.cpp
        ${CMAKE_SOURCE_DIR}/src/gfx/GpuResourceRegistry.cpp
        ${CMAKE_SOURCE_DIR}/src/core/FrameProfile.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Json.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Log.cpp
    )
    target_compile_definitions(SolarSystemWgpuTests PRIVATE SOLARSYSTEM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_include_directories(SolarSystemWgpuTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/glm")
        target_include_directories(SolarSystemWgpuTests PRIVATE ${CMAKE_SOURCE_DIR}/external/glm)
    endif()
    target_link_libraries(SolarSystemWgpuTests PRIVATE GTest::gtest_main dawn::webgpu_dawn OpenGL::GL GLEW::GLEW glfw)
    add_dependencies(SolarSystemWgpuTests wgsl_shaders)
    gtest_discover_tests(SolarSystemWgpuTests TEST_PREFIX "wgpu." DISCOVERY_MODE PRE_TEST)

    # The toolchain itself, over the whole corpus: any stage permutation that stops translating fails.
    add_test(NAME ShaderWgsl
             COMMAND wgsl_shader_compiler --shaders resource/shaders --out ${CMAKE_BINARY_DIR}/wgsl-check
                     --glslang ${SOLARSYSTEM_GLSLANG} --tint ${SOLARSYSTEM_TINT} --quiet
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

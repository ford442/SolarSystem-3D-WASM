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
    ${CMAKE_SOURCE_DIR}/src/Auxiliary_Modules/Ephemeris.cpp
    ${CMAKE_SOURCE_DIR}/src/QualitySettings.cpp
    ${CMAKE_SOURCE_DIR}/src/Auxiliary_Modules/PlanetManifestLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/Auxiliary_Modules/TextureLoadingQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/Auxiliary_Modules/WebResourceFetcher.cpp
)

add_executable(SolarSystemTests
    test_orbit_layout.cpp
    test_ephemeris.cpp
    test_quality_settings.cpp
    test_planet_manifest_loader.cpp
    test_texture_loading_queue.cpp
    test_asteroid_field.cpp
    stub_texture_image2d.cpp
    ${SOLARSYSTEM_TEST_SOURCES}
)

target_compile_definitions(SolarSystemTests PRIVATE SOLARSYSTEM_BUILD_TESTS)
target_include_directories(SolarSystemTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/glm
)
target_link_libraries(SolarSystemTests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(SolarSystemTests)

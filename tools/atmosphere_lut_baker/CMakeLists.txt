# Offline baker for resource/atmosphere/*.ktx2 (native only; no GL). Reads the catalog
# through the generated BodyCatalog.generated.h, so it needs no JSON parser.
add_executable(atmosphere_lut_baker
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/Solar_System/AtmosphereModel.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/ktx2_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/3rdparty/ktx2_writer.cpp
)
target_compile_features(atmosphere_lut_baker PRIVATE cxx_std_17)
target_include_directories(atmosphere_lut_baker PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(EXISTS "${CMAKE_SOURCE_DIR}/external/glm")
    target_include_directories(atmosphere_lut_baker PRIVATE ${CMAKE_SOURCE_DIR}/external/glm)
endif()

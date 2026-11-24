# GLSL -> SPIR-V -> WGSL for every shader stage permutation (docs/ARCHITECTURE.md §7.9).
# Native only and GL-free: ShaderSource assembles the same text the GL build compiles; glslang
# and Tint are external programs (SOLARSYSTEM_GLSLANG / SOLARSYSTEM_TINT: Tint is built with
# Dawn, glslang is in Dawn's third_party or a distro package).
#
# Also a project of its own, so the web build can make the host tool without a native tree:
#   cmake -S tools/wgsl_shader_compiler -B build-wgsl-tool && cmake --build build-wgsl-tool
if(NOT DEFINED PROJECT_NAME)
    cmake_minimum_required(VERSION 3.18)
    project(wgsl_shader_compiler CXX)
endif()
get_filename_component(SOLAR_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(wgsl_shader_compiler
    main.cpp
    ${SOLAR_REPO_ROOT}/src/gfx/ShaderSource.cpp
)
target_compile_features(wgsl_shader_compiler PRIVATE cxx_std_17)
target_include_directories(wgsl_shader_compiler PRIVATE ${SOLAR_REPO_ROOT}/src)

find_program(SOLARSYSTEM_GLSLANG NAMES glslang glslangValidator)
find_program(SOLARSYSTEM_TINT NAMES tint)

#!/usr/bin/env bash
# What the WebGPU backend needs on the host (docs/plans/WEBGPU_BACKEND_PLAN.md):
#   - Dawn (libwebgpu_dawn + DawnConfig.cmake), for the native-wgpu build and its tests;
#   - Tint (Dawn's `tint` CLI) and glslang, for the GLSL -> SPIR-V -> WGSL step every
#     WebGPU build runs (tools/wgsl_shader_compiler), the web one included.
# Dawn is checked out at the release emsdk's emdawnwebgpu port pins, so the native and web
# builds compile against the same webgpu.h / webgpu_cpp.h.
#
#   scripts/setup-webgpu-toolchain.sh [prefix]     # default: external/webgpu
#   cmake --preset native-wgpu                      # finds it there
#
# Needs git, python3, cmake, ninja and a C++20 compiler (clang preferred, as Dawn's CI uses).
# The build takes a while (Dawn is large); it is skipped when the prefix already has it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${1:-$ROOT/external/webgpu}"
JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"

# The Dawn release emdawnwebgpu is built from, read from emsdk's port file when emcc is around.
DAWN_TAG="${DAWN_TAG:-}"
if [ -z "$DAWN_TAG" ] && command -v emcc >/dev/null 2>&1; then
    PORT="$(dirname "$(readlink -f "$(command -v emcc)")")/tools/ports/emdawnwebgpu.py"
    if [ -f "$PORT" ]; then
        DAWN_TAG="$(sed -n "s/^_VERSION = '\(v[0-9.]*\)'.*/\1/p" "$PORT" | head -1)"
    fi
fi
DAWN_TAG="${DAWN_TAG:-v20260423.175430}" # emsdk 6.0.0's
echo "Dawn $DAWN_TAG -> $PREFIX"

CC_BIN="${CC:-$(command -v clang || command -v cc)}"
CXX_BIN="${CXX:-$(command -v clang++ || command -v c++)}"

if [ ! -f "$PREFIX/lib/cmake/Dawn/DawnConfig.cmake" ] || [ ! -x "$PREFIX/bin/tint" ]; then
    SRC="$PREFIX/src/dawn"
    if [ ! -d "$SRC/.git" ]; then
        git clone --depth 1 --branch "$DAWN_TAG" https://dawn.googlesource.com/dawn "$SRC"
    fi
    (cd "$SRC" && python3 tools/fetch_dawn_dependencies.py -s)
    # Vulkan (SwiftShader / lavapipe in CI) and the null backend; no GL, no window system:
    # the tests and golden captures render offscreen.
    cmake -S "$SRC" -B "$PREFIX/build/dawn" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC_BIN" -DCMAKE_CXX_COMPILER="$CXX_BIN" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED -DDAWN_ENABLE_INSTALL=ON \
        -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DTINT_BUILD_TESTS=OFF -DDAWN_BUILD_PROTOBUF=OFF \
        -DDAWN_ENABLE_VULKAN=ON -DDAWN_ENABLE_NULL=ON -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF \
        -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF -DDAWN_USE_GLFW=OFF \
        -DTINT_BUILD_CMD_TOOLS=ON -DTINT_BUILD_SPV_READER=ON -DTINT_BUILD_GLSL_WRITER=OFF \
        -DTINT_BUILD_GLSL_VALIDATOR=OFF
    cmake --build "$PREFIX/build/dawn" -j"$JOBS" --target webgpu_dawn tint
    cmake --build "$PREFIX/build/dawn" -j"$JOBS" --target install
    mkdir -p "$PREFIX/bin"
    cp "$PREFIX/build/dawn/tint" "$PREFIX/bin/tint"
else
    echo "Dawn and Tint already in $PREFIX"
fi

if [ ! -x "$PREFIX/bin/glslang" ]; then
    cmake -S "$PREFIX/src/dawn/third_party/glslang/src" -B "$PREFIX/build/glslang" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=ON
    cmake --build "$PREFIX/build/glslang" -j"$JOBS" --target install
else
    echo "glslang already in $PREFIX"
fi

echo "WebGPU toolchain ready: $PREFIX/{bin/tint,bin/glslang,lib/cmake/Dawn}"

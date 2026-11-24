#!/usr/bin/env bash
# GLSL -> SPIR-V -> WGSL into resource/shaders-wgsl/ (docs/ARCHITECTURE.md §7.9) for a web
# WebGPU build: an Emscripten tree cannot build and run a host tool, so build-web.sh calls this
# first (the native-wgpu build does the same through its wgsl_shaders target).
#
#   scripts/build-wgsl-shaders.sh [out-dir]
#
# glslang and tint come from SOLARSYSTEM_GLSLANG / SOLARSYSTEM_TINT, external/webgpu/bin
# (scripts/setup-webgpu-toolchain.sh) or PATH. Exits non-zero if any stage permutation fails.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/resource/shaders-wgsl}"
TOOLS="$ROOT/external/webgpu/bin"
GLSLANG="${SOLARSYSTEM_GLSLANG:-}"
TINT="${SOLARSYSTEM_TINT:-}"
[ -n "$GLSLANG" ] || { [ -x "$TOOLS/glslang" ] && GLSLANG="$TOOLS/glslang"; } || GLSLANG="$(command -v glslang || command -v glslangValidator || true)"
[ -n "$TINT" ] || { [ -x "$TOOLS/tint" ] && TINT="$TOOLS/tint"; } || TINT="$(command -v tint || true)"
if [ -z "$GLSLANG" ] || [ -z "$TINT" ]; then
    echo "error: glslang and tint are needed (run scripts/setup-webgpu-toolchain.sh)" >&2
    exit 1
fi

# The host compiler, never emcc: this tool runs at build time.
HOST_BUILD="$ROOT/build-wgsl-tool"
env -u CC -u CXX cmake -S "$ROOT/tools/wgsl_shader_compiler" -B "$HOST_BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$HOST_BUILD" -j"${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}" >/dev/null
cd "$ROOT"
"$HOST_BUILD/wgsl_shader_compiler" --shaders resource/shaders --out "$OUT" --glslang "$GLSLANG" --tint "$TINT" --quiet

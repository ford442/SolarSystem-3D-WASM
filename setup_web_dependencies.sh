#!/bin/bash
set -e

# --- NEW: Source Emscripten SDK Environment ---
# Ensure emcmake is available
if ! command -v emcmake &> /dev/null; then
    if [ -f "/content/build_space/emsdk/emsdk_env.sh" ]; then
        source /content/build_space/emsdk/emsdk_env.sh
    else
        echo "Warning: emsdk_env.sh not found and emcmake is not in PATH."
    fi
fi
# ----------------------------------------------

# Directory to hold external dependencies
EXTERNAL_DIR="external"
mkdir -p "$EXTERNAL_DIR"

# --- 1. GLM (Header Only) ---
GLM_DIR="$EXTERNAL_DIR/glm"
if [ ! -d "$GLM_DIR" ]; then
    echo "Fetching GLM..."
    git clone https://github.com/g-truc/glm.git "$GLM_DIR"
else
    echo "GLM already exists in $GLM_DIR"
fi

# --- 2. Assimp (Static Library for WebAssembly) ---
ASSIMP_DIR="$EXTERNAL_DIR/assimp"
if [ ! -d "$ASSIMP_DIR" ]; then
    echo "Fetching Assimp..."
    git clone https://github.com/assimp/assimp.git "$ASSIMP_DIR"
    # Checkout a stable version to avoid surprises
    cd "$ASSIMP_DIR"
    git checkout v5.3.1
    cd ../..
else
    echo "Assimp already exists in $ASSIMP_DIR"
fi

ASSIMP_BUILD_DIR="$ASSIMP_DIR/build-wasm"

# Clean previous failed builds to ensure new flags take effect
if [ -d "$ASSIMP_BUILD_DIR" ] && [ ! -f "$ASSIMP_BUILD_DIR/lib/libassimp.a" ]; then
    echo "Cleaning incomplete build..."
    rm -rf "$ASSIMP_BUILD_DIR"
fi

if [ ! -f "$ASSIMP_BUILD_DIR/lib/libassimp.a" ]; then
    echo "Building Assimp for WebAssembly..."
    mkdir -p "$ASSIMP_BUILD_DIR"
    cd "$ASSIMP_BUILD_DIR"

    # Configure Assimp
    # - Disable tests, tools, and exporters to save space/time
    # - Build static library
    # - Disable warnings as errors to pass strict compiler checks
    # - Explicitly disable Werror in compiler flags
    emcmake cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DASSIMP_BUILD_TESTS=OFF \
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF \
        -DASSIMP_NO_EXPORT=ON \
        -DASSIMP_BUILD_ZLIB=ON \
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF \
        -DASSIMP_BUILD_OBJ_IMPORTER=ON \
        -DASSIMP_BUILD_FBX_IMPORTER=ON \
        -DASSIMP_BUILD_GLTF_IMPORTER=ON \
        -DASSIMP_BUILD_COLLADA_IMPORTER=ON \
        -DASSIMP_WARNINGS_AS_ERRORS=OFF \
        -DCMAKE_CXX_FLAGS="-Wno-error -w" \
        -DCMAKE_INSTALL_PREFIX="$PWD/install"

    # Build
    emmake make -j55

    cd ../../..
else
    echo "Assimp library already built at $ASSIMP_BUILD_DIR/lib/libassimp.a"
fi

echo "Dependencies setup complete."

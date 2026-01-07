#!/bin/bash
# Build script for Emscripten/WebAssembly
set -e

echo "================================================"
echo "  SolarSystem 3D - WebAssembly Build Script"
echo "================================================"

# Default to sourcing emsdk unless skipped
SKIP_EMSDK=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --no-emsdk)
            SKIP_EMSDK=true
            shift
            ;;
    esac
done

if [ "$SKIP_EMSDK" = false ]; then
    if [ -f "/content/build_space/emsdk/emsdk_env.sh" ]; then
        source /content/build_space/emsdk/emsdk_env.sh
    else
        echo "Warning: emsdk_env.sh not found at /content/build_space/emsdk/emsdk_env.sh"
        echo "Assuming emcc is in PATH or usage of --no-emsdk is intended."
    fi
else
    echo "Skipping emsdk environment setup (as requested)."
fi

# Create build directory
BUILD_DIR="build-web"
mkdir -p "$BUILD_DIR"

# Get absolute path to the web directory for includes
WEB_INCLUDE_DIR="$(pwd)/web"

# Run CMake
echo "Running CMake configuration..."
# Use relative path for includes or valid absolute path derived from pwd
emcmake cmake -DCMAKE_CXX_FLAGS="-I/usr/local/include -I$WEB_INCLUDE_DIR" -B "$BUILD_DIR" .

# Build
echo "Building project..."
cd "$BUILD_DIR"
emmake make -j$(nproc)

# --- NEW: Deploy to Web Frontend ---
echo ""
echo "Deploying artifacts to web frontend..."
# Ensure directories exist
mkdir -p ../web/src
mkdir -p ../web/public

# 1. Copy the Glue Code to src (so it can be imported)
if [ -f "SolarSystem.js" ]; then
    cp SolarSystem.js ../web/src/
fi

# 2. Copy Assets to public (served at root URL)
if [ -f "SolarSystem.wasm" ]; then
    cp SolarSystem.wasm ../web/public/
fi
if [ -f "SolarSystem.data" ]; then
    cp SolarSystem.data ../web/public/
fi
# -----------------------------------------------

echo ""
echo "Build & Deployment Complete!"

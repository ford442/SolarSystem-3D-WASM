#!/bin/bash
# Build script for Emscripten/WebAssembly
set -e

echo "================================================"
echo "  SolarSystem 3D - WebAssembly Build Script"
echo "================================================"

source /content/build_space/emsdk/emsdk_env.sh

# Create build directory
BUILD_DIR="build-web"
mkdir -p "$BUILD_DIR"

# Run CMake
echo "Running CMake configuration..."
emcmake cmake -B "$BUILD_DIR" ../

# Build
echo "Building project..."
cd "$BUILD_DIR"
emmake make -j$(nproc 2>/dev/null || echo 4)

# --- NEW: Deploy to Web Frontend ---
echo ""
echo "Deploying artifacts to web frontend..."
# Ensure directories exist
mkdir -p ../web/src
mkdir -p ../web/public

# 1. Copy the Glue Code to src (so it can be imported)
cp SolarSystem.js ../web/src/

# 2. Copy Assets to public (served at root URL)
cp SolarSystem.wasm ../web/public/
cp SolarSystem.data ../web/public/
# -----------------------------------------------

echo ""
echo "Build & Deployment Complete!"

#!/bin/bash
# Build script for Emscripten/WebAssembly

set -e

echo "================================================"
echo "  SolarSystem 3D - WebAssembly Build Script"
echo "================================================"
echo ""

# Check if Emscripten is available
if ! command -v emcmake &> /dev/null; then
    echo "ERROR: Emscripten SDK not found!"
    echo "Please install Emscripten and run: source /path/to/emsdk/emsdk_env.sh"
    exit 1
fi

echo "Emscripten SDK found!"
emcc --version
echo ""

# Create build directory
BUILD_DIR="build-web"
echo "Creating build directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Run CMake with Emscripten
echo ""
echo "Running CMake configuration..."
emcmake cmake -B "$BUILD_DIR" .

# Build the project
echo ""
echo "Building project..."
cd "$BUILD_DIR"
emmake make -j$(nproc 2>/dev/null || echo 4)

echo ""
echo "================================================"
echo "  Build Complete!"
echo "================================================"
echo ""
echo "Output files:"
echo "  - SolarSystem.html  (Open this in a browser)"
echo "  - SolarSystem.js"
echo "  - SolarSystem.wasm"
echo "  - SolarSystem.data  (Preloaded assets)"
echo ""
echo "To run the application:"
echo "  1. cd $BUILD_DIR"
echo "  2. python3 -m http.server 8000"
echo "  3. Open http://localhost:8000/SolarSystem.html"
echo ""
echo "Note: A web server is required due to CORS restrictions"
echo "      when loading preloaded assets."
echo ""

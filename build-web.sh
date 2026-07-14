#!/bin/bash
# Build script for Emscripten/WebAssembly
set -e

echo "================================================"
echo "  SolarSystem 3D - WebAssembly Build Script"
echo "================================================"

# 1. Resolve Project Root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_JOBS="${BUILD_JOBS:-55}"
echo "Project Root: $PROJECT_ROOT"

# 2. Environment Setup (MOVED UP)
echo "------------------------------------------------"
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
    if command -v emcc &> /dev/null; then
        echo "Using Emscripten already available on PATH."
    elif [ -f "/content/build_space/emsdk/emsdk_env.sh" ]; then
        source /content/build_space/emsdk/emsdk_env.sh
    elif [ -f "/root/emsdk/emsdk_env.sh" ]; then
        source /root/emsdk/emsdk_env.sh
    else
        echo "Error: Emscripten is not on PATH and no known emsdk_env.sh was found."
        exit 1
    fi
else
    echo "Skipping emsdk environment setup (as requested)."
fi

# 3. Setup Dependencies
echo "------------------------------------------------"
echo "Checking and setting up dependencies..."
cd "$PROJECT_ROOT"

if [ -f "setup_web_dependencies.sh" ]; then
    chmod +x setup_web_dependencies.sh
    ./setup_web_dependencies.sh
else
    echo "Error: setup_web_dependencies.sh not found in $PROJECT_ROOT"
    exit 1
fi

# 4. Configure & Build
echo "------------------------------------------------"
# Create build directory in the Project Root
BUILD_DIR="$PROJECT_ROOT/build-web"
mkdir -p "$BUILD_DIR"

# Get absolute path to the web directory for includes
WEB_INCLUDE_DIR="$PROJECT_ROOT/web"

# Run CMake
echo "Running CMake configuration..."
emcmake cmake -DCMAKE_CXX_FLAGS="-I/usr/local/include -I$WEB_INCLUDE_DIR" -B "$BUILD_DIR" "$PROJECT_ROOT"

# Build
echo "Building project..."
cd "$BUILD_DIR"
emmake make -j"$BUILD_JOBS"

# 5. Deploy to Web Frontend
echo "------------------------------------------------"
echo "Deploying artifacts to web frontend..."
# Ensure directories exist
mkdir -p "$PROJECT_ROOT/web/src"
mkdir -p "$PROJECT_ROOT/web/public"

# Copy the Glue Code to src (so it can be imported)
if [ -f "SolarSystem.js" ]; then
    cp SolarSystem.js "$PROJECT_ROOT/web/src/"
    echo "Copied SolarSystem.js to web/src/"
fi

# Copy low-res textures to public/ so the Vite dev server can serve them when
# WebResourceFetcher tries to fetch from /solar-system/resource/textures_low/.
# These are also preloaded into the .data file (CMakeLists.txt), so they are
# available in MEMFS even without the dev server.
mkdir -p "$PROJECT_ROOT/web/public/resource/textures_low"
if [ -d "$PROJECT_ROOT/resource/textures_low" ]; then
    cp -r "$PROJECT_ROOT/resource/textures_low/." "$PROJECT_ROOT/web/public/resource/textures_low/"
    echo "Copied resource/textures_low to web/public/resource/textures_low/"
fi

# Keep the production-preview skybox fetch path valid when the external asset
# origin is not configured. Vite's SPA fallback otherwise answers missing DDS
# requests with index.html (HTTP 200), which would overwrite the valid MEMFS
# faces. These six tiny files are deterministic CI/local fallback fixtures.
mkdir -p "$PROJECT_ROOT/web/public/resource/textures/Main_SkyBox"
if [ -d "$PROJECT_ROOT/resource/textures_low/Main_SkyBox" ]; then
    cp -r "$PROJECT_ROOT/resource/textures_low/Main_SkyBox/." \
        "$PROJECT_ROOT/web/public/resource/textures/Main_SkyBox/"
    echo "Copied low-res skybox fallbacks to the runtime high-res fetch path"
fi

# Copy Assets to public (served at root URL)
if [ -f "SolarSystem.wasm" ]; then
    cp SolarSystem.wasm "$PROJECT_ROOT/web/public/"
    echo "Copied SolarSystem.wasm to web/public/"
fi
# Copy .data (Models, Shaders, Fonts)
if [ -f "SolarSystem.data" ]; then
    cp SolarSystem.data "$PROJECT_ROOT/web/public/"
    echo "Copied SolarSystem.data to web/public/"
fi

echo ""
echo "Build & Deployment Complete!"

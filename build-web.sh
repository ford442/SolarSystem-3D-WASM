#!/bin/bash
# Build script for Emscripten/WebAssembly
set -e

echo "================================================"
echo "  SolarSystem 3D - WebAssembly Build Script"
echo "================================================"

# 1. Resolve Project Root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"
echo "Project Root: $PROJECT_ROOT"

# Keep staged-loading / explorer / companion JSON in sync with the planet catalog.
if command -v node >/dev/null 2>&1; then
    echo "Generating planet metadata from resource/planets.catalog.json..."
    node "$PROJECT_ROOT/scripts/generate-planet-metadata.mjs"
else
    echo "Warning: node not found; skipping planet metadata generation."
fi

# 2. Environment Setup (MOVED UP)
echo "------------------------------------------------"
# Default to sourcing emsdk unless skipped
SKIP_EMSDK=false

# Parse arguments
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
for arg in "$@"; do
    case $arg in
        --no-emsdk)
            SKIP_EMSDK=true
            shift
            ;;
        --debug)
            BUILD_TYPE=Debug
            shift
            ;;
    esac
done
echo "CMake build type: $BUILD_TYPE"

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
# The web-release / web-debug presets in CMakePresets.json hold the configuration; this script
# adds the environment (emsdk, ccache) and the deploy step. Debug builds get their own
# directory so switching Debug<->Release no longer reconfigures one tree back and forth.
PRESET=web-release
BUILD_DIR="$PROJECT_ROOT/build-web"
if [ "$BUILD_TYPE" = "Debug" ]; then
    PRESET=web-debug
    BUILD_DIR="$PROJECT_ROOT/build-web-debug"
fi

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE="$BUILD_TYPE")
# CI sets SOLARSYSTEM_WERROR=ON so a new warning fails the build; locally it is off.
if [ -n "${SOLARSYSTEM_WERROR:-}" ]; then
    CMAKE_ARGS+=(-DSOLARSYSTEM_WERROR="$SOLARSYSTEM_WERROR")
fi

# emcc is a wrapper script, so ccache goes in through its own hook rather than CMake's launcher.
if [ -z "${EM_COMPILER_WRAPPER:-}" ] && command -v ccache >/dev/null 2>&1; then
    export EM_COMPILER_WRAPPER=ccache
    echo "Using ccache for emcc."
fi

# The presets find the toolchain file through $EMSDK. emsdk_env.sh and setup-emsdk set it; when
# only emcc is on PATH, derive it from emcc's location (<emsdk>/upstream/emscripten/emcc).
if [ -z "${EMSDK:-}" ] && command -v emcc >/dev/null 2>&1; then
    EMCC_DIR="$(dirname "$(readlink -f "$(command -v emcc)")")"
    if [ -f "$EMCC_DIR/cmake/Modules/Platform/Emscripten.cmake" ] && [ "$(basename "$(dirname "$EMCC_DIR")")" = "upstream" ]; then
        export EMSDK="$(cd "$EMCC_DIR/../.." && pwd)"
    fi
fi

if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]; then
    echo "Running CMake preset $PRESET..."
    (cd "$PROJECT_ROOT" && cmake --preset "$PRESET" "${CMAKE_ARGS[@]}")
    echo "Building project..."
    (cd "$PROJECT_ROOT" && cmake --build --preset "$PRESET" -j"$BUILD_JOBS")
else
    # A distro-packaged emscripten has no emsdk layout, so the preset's toolchain path does not
    # resolve. emcmake supplies the toolchain itself.
    echo "No emsdk layout found; configuring with emcmake instead of the $PRESET preset."
    mkdir -p "$BUILD_DIR"
    emcmake cmake "${CMAKE_ARGS[@]}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B "$BUILD_DIR" "$PROJECT_ROOT"
    echo "Building project..."
    cmake --build "$BUILD_DIR" -j"$BUILD_JOBS"
fi
cd "$BUILD_DIR"

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
mkdir -p "$PROJECT_ROOT/web/public/resource/textures_mid"
if [ -d "$PROJECT_ROOT/resource/textures_mid" ]; then
    cp -r "$PROJECT_ROOT/resource/textures_mid/." "$PROJECT_ROOT/web/public/resource/textures_mid/"
    echo "Copied resource/textures_mid to web/public/resource/textures_mid/"
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
echo "Artifact sizes ($BUILD_TYPE):"
if [ -f "$PROJECT_ROOT/web/public/SolarSystem.wasm" ]; then
    wc -c "$PROJECT_ROOT/web/public/SolarSystem.wasm" "$PROJECT_ROOT/web/public/SolarSystem.data" \
        "$PROJECT_ROOT/web/src/SolarSystem.js" 2>/dev/null || true
fi
# Release only: minified name -> original name, for reading Closure-renamed stacks.
if [ -f "SolarSystem.js.symbols" ]; then
    echo "Symbol map kept at $BUILD_DIR/SolarSystem.js.symbols (not deployed)"
fi
echo ""
echo "Build & Deployment Complete!"

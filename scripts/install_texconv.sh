#!/bin/bash
# Install Microsoft texconv for generate_web_textures.py
#
# Official DirectXTex releases ship Texconv.exe for Windows only. On Linux we
# download the release binary and invoke it through wine when available.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$SCRIPT_DIR/bin"
EXE_PATH="$INSTALL_DIR/Texconv.exe"
WRAPPER_PATH="$INSTALL_DIR/texconv"
RELEASE_URL="https://github.com/microsoft/DirectXTex/releases/download/oct2025/texconv.exe"

echo "================================================"
echo "  Installing texconv for web texture builds"
echo "================================================"

mkdir -p "$INSTALL_DIR"

if [ ! -f "$EXE_PATH" ]; then
    echo "Downloading Texconv.exe..."
    if command -v curl >/dev/null 2>&1; then
        curl -fL "$RELEASE_URL" -o "$EXE_PATH"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$EXE_PATH" "$RELEASE_URL"
    else
        echo "curl or wget is required to download texconv." >&2
        exit 1
    fi
else
    echo "Found existing $EXE_PATH"
fi

chmod +x "$EXE_PATH"

cat > "$WRAPPER_PATH" <<'EOF'
#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXE="$SCRIPT_DIR/Texconv.exe"

if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* || "$(uname -s)" == CYGWIN* ]]; then
    exec "$EXE" "$@"
fi

if command -v wine64 >/dev/null 2>&1; then
    exec wine64 "$EXE" "$@"
fi

if command -v wine >/dev/null 2>&1; then
    exec wine "$EXE" "$@"
fi

echo "texconv requires Wine on Linux. Install wine64 and re-run scripts/install_texconv.sh" >&2
exit 1
EOF

chmod +x "$WRAPPER_PATH"

echo ""
echo "Installed:"
echo "  $EXE_PATH"
echo "  $WRAPPER_PATH"
echo ""
echo "Next:"
echo "  python3 scripts/generate_web_textures.py \\"
echo "      --texconv $WRAPPER_PATH \\"
echo "      --input-dir /path/to/full-res/resource/textures \\"
echo "      --output-dir resource/textures_web"

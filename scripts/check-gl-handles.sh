#!/usr/bin/env bash
# Every GL object in src/ is owned by a gfx/GlHandle.h type (#149). Fail if anything else
# creates or deletes one by hand. glGenerateMipmap is not an object allocation, so it is
# not matched; comments in vendored 3rdparty/ code are skipped.
set -euo pipefail
cd "$(dirname "$0")/.."
pattern='\bgl(Gen(Buffers|VertexArrays|Textures|Framebuffers|Renderbuffers|Queries|Samplers|TransformFeedbacks)|Delete(Buffers|VertexArrays|Textures|Framebuffers|Renderbuffers|Queries|Samplers|TransformFeedbacks|Shader|Program)|Create(Shader|Program))\b'
if hits=$(grep -rnE "$pattern" src --include='*.cpp' --include='*.h' \
        | grep -v '^src/gfx/GlHandle\.h:' \
        | grep -vE '^src/3rdparty/[^:]+:[0-9]+:\s*//'); then
    echo "Raw GL object allocation outside src/gfx/GlHandle.h — use GlBuffer/GlTexture/... instead:" >&2
    echo "$hits" >&2
    exit 1
fi
echo "GL handles: OK (no raw glGen*/glDelete* outside gfx/GlHandle.h)"

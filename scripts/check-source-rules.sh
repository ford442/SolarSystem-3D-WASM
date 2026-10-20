#!/usr/bin/env bash
# Source-layout rules from #149: no `using namespace` in a header, no leftover
# Auxiliary_Modules/ grab-bag, and diagnostics through core/Log.h rather than iostreams.
# Vendored src/3rdparty/ is exempt.
set -euo pipefail
cd "$(dirname "$0")/.."
status=0

if hits=$(grep -rnE '^\s*using\s+namespace\b' src tests --include='*.h' --include='*.hpp' | grep -v '^src/3rdparty/'); then
    echo "'using namespace' in a header (qualify the names, or move it into the .cpp):" >&2
    echo "$hits" >&2
    status=1
fi

if [ -e src/Auxiliary_Modules ]; then
    echo "src/Auxiliary_Modules/ is back; files belong in gfx/ render/ assets/ astro/ core/ (ARCHITECTURE §7.2)" >&2
    status=1
fi

if hits=$(grep -rnE '\bstd::(cout|cerr|clog)\b|^\s*(cout|cerr)\s*<<' src --include='*.cpp' --include='*.h' \
        | grep -v '^src/3rdparty/' | grep -vE '^[^:]+:[0-9]+:\s*//'); then
    echo "iostream logging in src/ — use LOG_DEBUG/INFO/WARN/ERROR from core/Log.h:" >&2
    echo "$hits" >&2
    status=1
fi

[ "$status" -eq 0 ] && echo "Source rules: OK (no using-namespace in headers, no Auxiliary_Modules/, no iostream logging)"
exit "$status"

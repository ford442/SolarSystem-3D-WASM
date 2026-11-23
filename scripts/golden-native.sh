#!/usr/bin/env bash
# Native golden images: draw a fixed set of shots on every quality tier with one binary and
# write them as PPMs, so two builds can be compared pixel for pixel (scripts/golden-compare.py).
#
#   scripts/golden-native.sh <SolarSystem binary> <out dir>
#   scripts/golden-compare.py <dir A> <dir B>
#
# Each shot is SOLARSYSTEM_GOLDEN (Application::RunGoldenCapture): fixed 1/30 s frame steps,
# a pinned date, jobs inline (SOLARSYSTEM_JOBS=0), the HUD's FPS held at 60. Runs under
# Xvfb + llvmpipe like CI's smoke test. A checkout has no deploy textures or the moon/ring
# models, so unless GOLDEN_ROOT points at a tree that has them, a scratch copy of resource/ is
# staged in <out dir>/.root with stand-ins: the shipped _Low placeholders under their
# full-res names, a 4x4 grey DDS for anything else, the sphere for Phobos/Deimos and a flat
# annulus for each ring. Shaders are symlinked, so the build under test reads its own.
set -euo pipefail

if [ $# -lt 2 ]; then
    sed -n 2,16p "$0"
    exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
out="$(mkdir -p "$2" && cd "$2" && pwd)"
size="${GOLDEN_SIZE:-960x540}"
tiers="${GOLDEN_TIERS:-low medium full}"

stage_root() {
    local root="$1"
    rm -rf "$root"
    mkdir -p "$root"
    cp -r "$repo/resource" "$root/"
    rm -rf "$root/resource/shaders"
    ln -s "$repo/resource/shaders" "$root/resource/shaders"
    mkdir -p "$root/resource/textures/Main_SkyBox"
    cp "$repo"/resource/textures_low/Main_SkyBox/*.dds "$root/resource/textures/Main_SkyBox/"
    local f
    for f in "$repo"/resource/textures_low/*_Low.dds; do
        local name
        name="$(basename "$f" _Low.dds)"
        [ -e "$root/resource/textures/$name.dds" ] || cp "$f" "$root/resource/textures/$name.dds"
    done
    # Any other texture the sources or catalogs name that the checkout lacks: 4x4 grey.
    (cd "$repo" && grep -rhoE 'resource/[A-Za-z0-9_/]+\.dds' src resource --include='*.cpp' --include='*.h' \
        --include='*.json' | sort -u) | while read -r f; do
        if [ ! -e "$root/$f" ]; then
            mkdir -p "$(dirname "$root/$f")"
            python3 "$repo/scripts/make_placeholder_dds.py" "$root/$f" 808080 >/dev/null
        fi
    done
    for f in phobos deimos; do
        [ -e "$root/resource/models/$f.obj" ] || cp "$root/resource/models/sphere.obj" "$root/resource/models/$f.obj"
    done
    python3 - "$root/resource/models" <<'PY'
import math, os, sys
for name, inner, outer in (("saturn_ring", 22.0, 43.7), ("uranus_ring", 12.6, 16.0)):
    path = os.path.join(sys.argv[1], name + ".obj")
    if os.path.exists(path):
        continue
    n = 128
    with open(path, "w") as f:
        for i in range(n):
            a = 2 * math.pi * i / n
            for r in (inner, outer):
                f.write(f"v {r * math.cos(a):.5f} 0 {r * math.sin(a):.5f}\n")
        for i in range(n):
            f.write(f"vt {i / n:.5f} 0\nvt {i / n:.5f} 1\n")
        f.write("vn 0 1 0\n")
        for i in range(n):
            j = (i + 1) % n
            a, b, c, d = 2 * i + 1, 2 * i + 2, 2 * j + 1, 2 * j + 2
            f.write(f"f {a}/{a}/1 {c}/{c}/1 {b}/{b}/1\nf {b}/{b}/1 {c}/{c}/1 {d}/{d}/1\n")
PY
}

root="${GOLDEN_ROOT:-}"
if [ -z "$root" ]; then
    root="$out/.root"
    stage_root "$root"
fi

export LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460
export SDL_AUDIODRIVER=dummy SOLARSYSTEM_JOBS=0 SOLARSYSTEM_SHADER_HOT_RELOAD=0

# name | extra environment | SOLARSYSTEM_GOLDEN keys (frames, out and the date are added)
jd=2460409.5 # 2024-04-08
shots=(
    "start||frames=30"
    "earth||frames=300,focus=3"
    "saturn||frames=300,focus=6"
    "jupiter-fields||frames=300,focus=5,fields=1"
    "earth-wind|SOLARSYSTEM_SOLAR_WIND=1 SOLARSYSTEM_KP=7|frames=300,focus=3"
    "realistic-earth|SOLARSYSTEM_SCALE=realistic|frames=300,focus=3"
    "observe|SOLARSYSTEM_OBSERVE=51.5,0.0,2460409.5,180,20,60|frames=30"
)

missing=0
for tier in $tiers; do
    for shot in "${shots[@]}"; do
        IFS='|' read -r name extra keys <<<"$shot"
        image="$out/$tier-$name.ppm"
        rm -f "$image"
        # shellcheck disable=SC2086 # `extra` is a list of VAR=value words
        (cd "$root" && env $extra SOLARSYSTEM_QUALITY="$tier" \
            SOLARSYSTEM_GOLDEN="$keys,jd=$jd,out=$image" \
            timeout 600s xvfb-run -a -s "-screen 0 ${size}x24" "$binary" >"$out/$tier-$name.log" 2>&1) || true
        if [ -s "$image" ]; then
            echo "wrote $tier-$name"
        else
            echo "FAILED $tier-$name (see $out/$tier-$name.log)"
            missing=1
        fi
    done
done
exit "$missing"

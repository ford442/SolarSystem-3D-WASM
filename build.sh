#!/usr/bin/env bash
# Native build: a thin wrapper over the `native-release` preset in CMakePresets.json
# (Ninja, Release, output in build/). Extra arguments go to the configure step, e.g.
#   ./build.sh -DSOLARSYSTEM_WERROR=ON
# For Debug or the unit tests use the other presets directly:
#   cmake --preset native-debug && cmake --build --preset native-debug
#   cmake --workflow --preset native-tests
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
cmake --preset native-release "$@"
cmake --build --preset native-release

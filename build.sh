#!/usr/bin/env bash
set -euo pipefail

mkdir -p build
cd build
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

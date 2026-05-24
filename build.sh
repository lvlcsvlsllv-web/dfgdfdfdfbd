#!/usr/bin/env bash
# Build script for Silent Woods.
# Fetches miniaudio.h into ./vendor and builds the game via CMake.
# raylib will be fetched/built automatically by CMake if not installed system-wide.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

MINIAUDIO_URL="https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h"
mkdir -p vendor
if [ ! -f vendor/miniaudio.h ]; then
    echo "==> Fetching miniaudio.h ..."
    if command -v curl >/dev/null 2>&1; then
        curl -fL "$MINIAUDIO_URL" -o vendor/miniaudio.h
    elif command -v wget >/dev/null 2>&1; then
        wget -O vendor/miniaudio.h "$MINIAUDIO_URL"
    else
        echo "Need curl or wget to fetch miniaudio.h" >&2
        exit 1
    fi
fi

mkdir -p build
cd build
# CMAKE_POLICY_VERSION_MINIMUM=3.5 is needed because raylib 5.0's CMakeLists.txt
# uses an old cmake_minimum_required value that newer CMake (>=4) rejects.
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..
cmake --build . -j

echo
echo "==> Done. Run with:"
echo "    ./build/silent_woods"

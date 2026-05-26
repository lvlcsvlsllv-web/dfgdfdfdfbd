#!/usr/bin/env bash
# Build script for Silent Woods.
# Fetches miniaudio.h into ./vendor and builds the game via CMake.
# raylib is fetched/built automatically by CMake if not installed system-wide.

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
# CMAKE_POLICY_VERSION_MINIMUM=3.5 keeps newer CMake (>=4) compatible with
# older project files inside raylib's dependency tree.
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..
cmake --build . -j

echo
echo "==> Done. Run with:"
if [ -f silent_woods.exe ]; then
    echo "    ./build/silent_woods.exe"
else
    echo "    ./build/silent_woods"
fi

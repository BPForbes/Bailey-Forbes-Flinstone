#!/bin/sh
# Fetch and build SDL2 into deps/install. Idempotent.
# Requires: curl, cmake, C and C++ compilers (gcc/g++, or working clang)
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALL_PREFIX="${DEPS_ROOT}/deps/install"
BUILD_DIR="${DEPS_ROOT}/deps/build"
SDL_VERSION="${SDL_VERSION:-2.30.0}"
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz"

mkdir -p "${DEPS_ROOT}/deps/build" "${DEPS_ROOT}/deps/download"

if [ -f "${INSTALL_PREFIX}/lib/libSDL2.a" ] || [ -f "${INSTALL_PREFIX}/lib/libSDL2.so" ] 2>/dev/null; then
    echo "[deps] SDL2 already installed at ${INSTALL_PREFIX}"
    exit 0
fi

ARCHIVE="${DEPS_ROOT}/deps/download/SDL2-${SDL_VERSION}.tar.gz"
if [ ! -f "$ARCHIVE" ]; then
    echo "[deps] Downloading SDL2 ${SDL_VERSION}..."
    curl -sL "$SDL_URL" -o "$ARCHIVE" || { echo "[deps] Download failed. Try: apt install libsdl2-dev"; exit 1; }
fi

echo "[deps] Extracting SDL2..."
rm -rf "${BUILD_DIR}/SDL2-${SDL_VERSION}"
tar xzf "$ARCHIVE" -C "$BUILD_DIR"
SDL_SRC="${BUILD_DIR}/SDL2-${SDL_VERSION}"

echo "[deps] Building SDL2 (cmake)..."
cd "$SDL_SRC"
cmake -B build -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DSDL_STATIC=OFF -DSDL_SHARED=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -Wno-dev || {
    echo "[deps] CMake failed. Install: apt install build-essential cmake"
    echo "[deps] Or use system SDL2: apt install libsdl2-dev && make vm-sdl"
    exit 1
}
cmake --build build
cmake --install build

echo "[deps] SDL2 installed to ${INSTALL_PREFIX}"

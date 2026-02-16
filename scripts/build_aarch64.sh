#!/bin/bash
# Build script for aarch64 architecture

set -e

BUILD_DIR="build-aarch64"
mkdir -p ${BUILD_DIR}

cd ${BUILD_DIR}

# Set cross-compiler if not in PATH
if [ -z "$CROSS_COMPILE" ]; then
    export CROSS_COMPILE=aarch64-linux-gnu-
fi

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/aarch64.cmake \
    -DTARGET_ARCH=aarch64 \
    -DCMAKE_BUILD_TYPE=Release

cmake --build . -j$(nproc)

echo "Build complete. Binaries are in ${BUILD_DIR}/"

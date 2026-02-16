#!/bin/bash
# Build script for x86_64 architecture

set -e

BUILD_DIR="build-x86_64"
mkdir -p ${BUILD_DIR}

cd ${BUILD_DIR}

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/x86_64.cmake \
    -DTARGET_ARCH=x86_64 \
    -DCMAKE_BUILD_TYPE=Release

cmake --build . -j$(nproc)

echo "Build complete. Binaries are in ${BUILD_DIR}/"

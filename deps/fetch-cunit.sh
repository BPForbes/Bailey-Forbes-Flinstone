#!/bin/sh
# Fetch and build CUnit into deps/install. Idempotent.
# Requires: curl, C compiler (gcc), autotools (automake, libtool)
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALL_PREFIX="${DEPS_ROOT}/deps/install"
BUILD_DIR="${DEPS_ROOT}/deps/build"
CUNIT_VERSION="${CUNIT_VERSION:-2.1-3}"
CUNIT_URL="https://downloads.sourceforge.net/project/cunit/CUnit/${CUNIT_VERSION}/CUnit-${CUNIT_VERSION}.tar.bz2"

mkdir -p "${BUILD_DIR}" "${DEPS_ROOT}/deps/download"

if [ -f "${INSTALL_PREFIX}/lib/libcunit.a" ] || [ -f "${INSTALL_PREFIX}/lib/libcunit.so" ] 2>/dev/null; then
    echo "[deps] CUnit already installed at ${INSTALL_PREFIX}"
    exit 0
fi

ARCHIVE="${DEPS_ROOT}/deps/download/CUnit-${CUNIT_VERSION}.tar.bz2"
if [ ! -f "$ARCHIVE" ]; then
    echo "[deps] Downloading CUnit ${CUNIT_VERSION}..."
    curl -sL "$CUNIT_URL" -o "$ARCHIVE" -L || {
        echo "[deps] Download failed. Try: apt install libcunit1-dev"
        exit 1
    }
fi

echo "[deps] Extracting CUnit..."
rm -rf "${BUILD_DIR}/CUnit-${CUNIT_VERSION}"
tar xjf "$ARCHIVE" -C "$BUILD_DIR"
CUNIT_SRC="${BUILD_DIR}/CUnit-${CUNIT_VERSION}"

echo "[deps] Building CUnit (configure/make)..."
cd "$CUNIT_SRC"
[ -f configure ] || ( ./bootstrap 2>/dev/null || autoreconf -fi )
./configure --prefix="$INSTALL_PREFIX" --enable-shared || {
    echo "[deps] Configure failed. Install: apt install build-essential automake libtool"
    echo "[deps] Or use system CUnit: apt install libcunit1-dev"
    exit 1
}
make
make install

echo "[deps] CUnit installed to ${INSTALL_PREFIX}"

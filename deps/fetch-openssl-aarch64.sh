#!/bin/sh
# Build OpenSSL (libcrypto) for AArch64 cross-linking into deps/install-aarch64.
# Idempotent. Used when apt multiarch (libssl-dev:arm64) is unavailable.
# Requires: curl, tar, perl, make, aarch64-linux-gnu-gcc
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALL_PREFIX="${DEPS_ROOT}/deps/install-aarch64"
BUILD_DIR="${DEPS_ROOT}/deps/build/openssl-aarch64"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.0.15}"
OPENSSL_TAR="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_TAR}"
OPENSSL_SHA256="${OPENSSL_SHA256:-23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533}"

CC_ARM="${CC_ARM:-aarch64-linux-gnu-gcc}"
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

verify_sha256() {
    file="$1"
    expected="$2"
    actual="$(sha256sum "$file" | awk '{print $1}')"
    if [ "$actual" != "$expected" ]; then
        echo "[deps] SHA-256 mismatch for $(basename "$file")" >&2
        echo "[deps]   expected: $expected" >&2
        echo "[deps]   actual:   $actual" >&2
        exit 1
    fi
}

mkdir -p "${DEPS_ROOT}/deps/download" "${BUILD_DIR}"

if [ -f "${INSTALL_PREFIX}/lib/libcrypto.a" ] && \
   [ -f "${INSTALL_PREFIX}/include/openssl/opensslconf.h" ]; then
    echo "[deps] OpenSSL (AArch64) already installed at ${INSTALL_PREFIX}"
    exit 0
fi

command -v "${CC_ARM}" >/dev/null 2>&1 || {
    echo "[deps] ${CC_ARM} not found. Install: apt install gcc-aarch64-linux-gnu"
    exit 1
}
command -v perl >/dev/null 2>&1 || {
    echo "[deps] perl not found (required by OpenSSL Configure)"
    exit 1
}

ARCHIVE="${DEPS_ROOT}/deps/download/${OPENSSL_TAR}"
if [ ! -f "$ARCHIVE" ]; then
    echo "[deps] Downloading OpenSSL ${OPENSSL_VERSION}..."
    curl -fsSL "$OPENSSL_URL" -o "$ARCHIVE"
fi
verify_sha256 "$ARCHIVE" "$OPENSSL_SHA256"

echo "[deps] Extracting OpenSSL..."
rm -rf "${BUILD_DIR}/openssl-${OPENSSL_VERSION}"
tar -xzf "$ARCHIVE" -C "${BUILD_DIR}"
SRC="${BUILD_DIR}/openssl-${OPENSSL_VERSION}"

echo "[deps] Cross-compiling OpenSSL for AArch64 (static libcrypto)..."
cd "$SRC"
./Configure linux-aarch64 \
    --cross-compile-prefix="${CROSS_PREFIX}" \
    --prefix="${INSTALL_PREFIX}" \
    --openssldir="${INSTALL_PREFIX}/ssl" \
    no-shared no-tests
make -j"$(nproc 2>/dev/null || echo 2)"
make install_sw

echo "[deps] OpenSSL (AArch64) installed to ${INSTALL_PREFIX}"

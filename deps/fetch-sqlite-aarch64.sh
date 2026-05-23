#!/bin/sh
# Build SQLite amalgamation for AArch64 cross-linking into deps/install-aarch64.
# Idempotent. Used when apt multiarch (libsqlite3-dev:arm64) is unavailable (GitHub Actions).
# Requires: curl, unzip, aarch64-linux-gnu-gcc, aarch64-linux-gnu-ar
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALL_PREFIX="${DEPS_ROOT}/deps/install-aarch64"
BUILD_DIR="${DEPS_ROOT}/deps/build/sqlite-aarch64"
SQLITE_VERSION="${SQLITE_VERSION:-3460100}"
SQLITE_ZIP="sqlite-amalgamation-${SQLITE_VERSION}.zip"
# SQLite publishes amalgamation zips under https://www.sqlite.org/<year>/; probe recent years.
SQLITE_SHA256="${SQLITE_SHA256:-77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784}"

CC_ARM="${CC_ARM:-aarch64-linux-gnu-gcc}"
AR_ARM="${AR_ARM:-aarch64-linux-gnu-ar}"

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

sqlite_download_url() {
    year="$1"
    printf '%s' "https://www.sqlite.org/${year}/${SQLITE_ZIP}"
}

mkdir -p "${DEPS_ROOT}/deps/download" "${BUILD_DIR}"

if [ -f "${INSTALL_PREFIX}/lib/libsqlite3.a" ] && [ -f "${INSTALL_PREFIX}/include/sqlite3.h" ]; then
    echo "[deps] SQLite (AArch64) already installed at ${INSTALL_PREFIX}"
    exit 0
fi

command -v "${CC_ARM}" >/dev/null 2>&1 || {
    echo "[deps] ${CC_ARM} not found. Install: apt install gcc-aarch64-linux-gnu"
    exit 1
}
command -v "${AR_ARM}" >/dev/null 2>&1 || {
    echo "[deps] ${AR_ARM} not found. Install: apt install binutils-aarch64-linux-gnu"
    exit 1
}

ARCHIVE="${DEPS_ROOT}/deps/download/${SQLITE_ZIP}"
if [ ! -f "$ARCHIVE" ]; then
    SQLITE_URL=""
    for year in ${SQLITE_YEAR:-} 2025 2024 2023; do
        [ -n "$year" ] || continue
        candidate="$(sqlite_download_url "$year")"
        if curl -fsI "$candidate" >/dev/null 2>&1; then
            SQLITE_URL="$candidate"
            break
        fi
    done
    if [ -z "$SQLITE_URL" ]; then
        echo "[deps] Could not find ${SQLITE_ZIP} on sqlite.org (tried recent release years)" >&2
        exit 1
    fi
    echo "[deps] Downloading SQLite amalgamation ${SQLITE_VERSION}..."
    curl -fsSL "$SQLITE_URL" -o "$ARCHIVE"
fi
verify_sha256 "$ARCHIVE" "$SQLITE_SHA256"

echo "[deps] Extracting SQLite..."
rm -rf "${BUILD_DIR}/sqlite-amalgamation-${SQLITE_VERSION}"
unzip -q -o "$ARCHIVE" -d "${BUILD_DIR}"
SRC="${BUILD_DIR}/sqlite-amalgamation-${SQLITE_VERSION}"

echo "[deps] Cross-compiling libsqlite3.a for AArch64..."
rm -f "${SRC}/sqlite3.o" "${SRC}/libsqlite3.a"
"${CC_ARM}" -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION=1 -c "${SRC}/sqlite3.c" -o "${SRC}/sqlite3.o"
"${AR_ARM}" rcs "${SRC}/libsqlite3.a" "${SRC}/sqlite3.o"

mkdir -p "${INSTALL_PREFIX}/lib" "${INSTALL_PREFIX}/include"
cp "${SRC}/libsqlite3.a" "${INSTALL_PREFIX}/lib/"
cp "${SRC}/sqlite3.h" "${SRC}/sqlite3ext.h" "${INSTALL_PREFIX}/include/"

echo "[deps] SQLite (AArch64) installed to ${INSTALL_PREFIX}"

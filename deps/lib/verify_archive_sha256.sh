#!/bin/sh
# Shared SHA-256 verification for dependency archives (issue #218).
# Source from fetch-*.sh: . "$SCRIPT_DIR/lib/verify_archive_sha256.sh"
verify_archive_sha256() {
    file="$1"
    expected="$2"
    if [ -z "$file" ] || [ -z "$expected" ]; then
        echo "[deps] verify_archive_sha256: missing file or expected digest" >&2
        return 1
    fi
    if [ ! -f "$file" ]; then
        echo "[deps] verify_archive_sha256: not a file: $file" >&2
        return 1
    fi
    actual="$(sha256sum "$file" | awk '{print $1}')"
    if [ "$actual" != "$expected" ]; then
        echo "[deps] SHA-256 mismatch for $(basename "$file")" >&2
        echo "[deps]   expected: $expected" >&2
        echo "[deps]   actual:   $actual" >&2
        return 1
    fi
    return 0
}

#!/bin/sh
# CI: every deps/fetch-*.sh that downloads an archive must source verify_archive_sha256.sh
# and call verify_archive_sha256 after the archive path is known (issue #218).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
for script in "$ROOT"/deps/fetch-*.sh; do
    [ -f "$script" ] || continue
    base="$(basename "$script")"
    if ! grep -q 'verify_archive_sha256' "$script" 2>/dev/null; then
        if grep -qE 'curl.*-o.*ARCHIVE|curl.*-o.*"\$ARCHIVE"' "$script" 2>/dev/null; then
            echo "check_deps_fetch_verify_sha256: $base downloads but does not call verify_archive_sha256" >&2
            fail=1
        fi
        continue
    fi
    if ! grep -q 'verify_archive_sha256\.sh' "$script" 2>/dev/null; then
        echo "check_deps_fetch_verify_sha256: $base must source deps/lib/verify_archive_sha256.sh" >&2
        fail=1
    fi
done
if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check_deps_fetch_verify_sha256: ok"

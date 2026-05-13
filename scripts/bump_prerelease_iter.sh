#!/usr/bin/env bash
# Increment PRERELEASE_ITER in a prerelease .ver (PRERELEASE=1).
# Usage: ./scripts/bump_prerelease_iter.sh path/to/file.ver
set -euo pipefail
f="${1:-}"
[[ -n "$f" && -f "$f" ]] || {
  echo "usage: $0 path/to/file.ver" >&2
  exit 1
}
if ! grep -qE '^[[:space:]]*(int[[:space:]]+)?PRERELEASE=1([[:space:]]|$)' "$f"; then
  echo "$0: $f must contain PRERELEASE=1" >&2
  exit 1
fi
if ! grep -qE '^[[:space:]]*(int[[:space:]]+)?PRERELEASE_ITER=' "$f"; then
  echo "$0: $f must contain PRERELEASE_ITER=" >&2
  exit 1
fi
val=$(grep -E '^[[:space:]]*(int[[:space:]]+)?PRERELEASE_ITER=' "$f" | head -1 |
  sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/')
new=$((val + 1))
sed -i -E "s/^([[:space:]]*(int[[:space:]]+)?PRERELEASE_ITER=)[0-9]+/\1${new}/" "$f"
echo "$0: PRERELEASE_ITER $val -> $new in $f"

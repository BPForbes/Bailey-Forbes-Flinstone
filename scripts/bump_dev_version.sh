#!/usr/bin/env bash
# Increment DEV_VERSION in a .ver file (typically under preproduction A.B.C/ with PRERELEASE=1).
# Usage: ./scripts/bump_dev_version.sh path/to/file.ver
set -euo pipefail
f="${1:-}"
[[ -n "$f" && -f "$f" ]] || {
  echo "usage: $0 path/to/file.ver" >&2
  exit 1
}
if ! grep -qE '^[[:space:]]*(int[[:space:]]+)?DEV_VERSION=' "$f"; then
  echo "$0: $f must contain DEV_VERSION=" >&2
  exit 1
fi
val=$(grep -E '^[[:space:]]*(int[[:space:]]+)?DEV_VERSION=' "$f" | head -1 |
  sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/')
[[ -n "$val" ]] || {
  echo "$0: could not read DEV_VERSION in $f" >&2
  exit 1
}
new=$((val + 1))
sed -i -E "s/^([[:space:]]*(int[[:space:]]+)?DEV_VERSION=)[0-9]+/\1${new}/" "$f"
echo "$0: DEV_VERSION $val -> $new in $f"

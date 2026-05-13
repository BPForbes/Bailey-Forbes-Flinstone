#!/usr/bin/env bash
# Increment DEV_PHASE in a single .ver file, or insert DEV_PHASE=1 after the
# RELEASE_VERSION / MINOR_VERSION / VERSION_PATCH line when the key is absent.
# Filenames stay A_B_C_slug.ver (D is never part of the basename).
# Usage: ./scripts/bump_dev_phase_in_ver.sh path/to/file.ver
set -euo pipefail
f="${1:-}"
if [[ -z "$f" || ! -f "$f" ]]; then
  echo "usage: $0 path/to/file.ver" >&2
  exit 1
fi
case "$f" in
  *.ver) ;;
  *)
    echo "error: expected a .ver file" >&2
    exit 1
    ;;
esac

if grep -qE '^[[:space:]]*(int[[:space:]]+)?DEV_PHASE=[[:space:]]*[0-9]+[[:space:]]*$' "$f"; then
  val=$(grep -E '^[[:space:]]*(int[[:space:]]+)?DEV_PHASE=[[:space:]]*[0-9]+[[:space:]]*$' "$f" | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/')
  new=$((val + 1))
  sed -i -E "s/^([[:space:]]*(int[[:space:]]+)?DEV_PHASE=)[0-9]+/\1${new}/" "$f"
  echo "$0: DEV_PHASE $val -> $new in $f"
  exit 0
fi

tmp="${f}.tmp.$$"
awk '
  BEGIN { inserted = 0 }
  /^[[:space:]]*(int[[:space:]]+)?RELEASE_VERSION=/ ||
  /^[[:space:]]*(int[[:space:]]+)?MINOR_VERSION=/ ||
  /^[[:space:]]*(int[[:space:]]+)?VERSION_PATCH=/ {
    print
    if (!inserted) {
      print "DEV_PHASE=1"
      inserted = 1
    }
    next
  }
  { print }
  END {
    if (!inserted) {
      print "bump_dev_phase_in_ver: no RELEASE_VERSION / MINOR_VERSION / VERSION_PATCH line" > "/dev/stderr"
      exit 1
    }
  }
' "$f" >"$tmp"
mv "$tmp" "$f"
echo "$0: inserted DEV_PHASE=1 in $f"

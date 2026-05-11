#!/usr/bin/env bash
# Append RELEASE_DATE=YYYY-MM-DD to each *.ver under version/locked and
# version/entries when that key is absent. Intended to run after
# finalize_version_locked.sh (entries and locked are mirrors) so merge-time
# dates are recorded before gen_version_def.sh and changelog generation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LCK="$ROOT/version/locked"
ENT="$ROOT/version/entries"
d="$(date +%Y-%m-%d)"

stamp_if_missing() {
  local f=$1
  [[ -f "$f" ]] || return 0
  if grep -qE '^[[:space:]]*(int[[:space:]]+)?RELEASE_DATE=' "$f"; then
    return 0
  fi
  printf '\nRELEASE_DATE=%s\n' "$d" >>"$f"
}

shopt -s nullglob
for f in "$LCK"/*.ver; do
  stamp_if_missing "$f"
  bf="$ENT/$(basename "$f")"
  stamp_if_missing "$bf"
done

echo "stamp_version_release_date: stamped missing RELEASE_DATE (as $d) under $LCK and $ENT"

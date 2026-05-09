#!/usr/bin/env bash
# Ensure userland/shell/version_def.h matches the highest A.B.C among version/entries/*.ver.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEF="$ROOT/userland/shell/version_def.h"

# pick_int extracts the first numeric value from a `#define <name> <value>` line in "$DEF"; the macro name is passed as the sole argument.
pick_int() {
  local name="$1"
  grep -E "^#define ${name}[[:space:]]+" "$DEF" | awk '{print $3}' | head -1
}

HM="$(pick_int VERSION_MAJOR)"
HS="$(pick_int VERSION_STANDARD)"
HP="$(pick_int VERSION_PATCH)"

shopt -s nullglob
files=("$ROOT/version/entries"/*.ver)
if (( ${#files[@]} == 0 )); then
  echo "check_version_header_matches_entries: no .ver files; skipping"
  exit 0
fi

bm=-1 bs=-1 br=-1
for f in "${files[@]}"; do
  m=$(grep -E '^[[:space:]]*(int[[:space:]]+)?(MAJOR_VERSION|VERSION_MAJOR)=' "$f" | head -1 | sed -E 's/^[^=]*=([0-9]+).*/\1/')
  s=$(grep -E '^[[:space:]]*(int[[:space:]]+)?(STANDARD_VERSION|VERSION_STANDARD)=' "$f" | head -1 | sed -E 's/^[^=]*=([0-9]+).*/\1/')
  r=$(grep -E '^[[:space:]]*(int[[:space:]]+)?(RELEASE_VERSION|MINOR_VERSION|VERSION_PATCH)=' "$f" | head -1 | sed -E 's/^[^=]*=([0-9]+).*/\1/')
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "check_version_header_matches_entries: could not parse semver from $f" >&2
    exit 1
  fi
  if (( bm < 0 )) || \
     (( m > bm )) || \
     (( m == bm && s > bs )) || \
     (( m == bm && s == bs && r > br )); then
    bm=$m bs=$s br=$r
  fi
done

if [[ "$HM" != "$bm" || "$HS" != "$bs" || "$HP" != "$br" ]]; then
  echo "error: version_def.h ($HM.$HS.$HP) must match highest entry ($bm.$bs.$br)" >&2
  echo "Update userland/shell/version_def.h or adjust version/entries/*.ver." >&2
  exit 1
fi

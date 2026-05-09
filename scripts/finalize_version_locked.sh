#!/usr/bin/env bash
# Finalize published versions: copy version/entries (WIP) → version/locked (frozen).
# After this, run `make` or ./scripts/gen_version_def.sh so userland/shell/version_def.h
# reflects the highest A.B.C among version/locked/*.ver.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"
LCK="$ROOT/version/locked"
mkdir -p "$ENT" "$LCK"
find "$LCK" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
if [[ -d "$ENT" ]]; then
  shopt -s dotglob nullglob
  # shellcheck disable=SC2086
  cp -a "$ENT"/. "$LCK"/
fi
echo "finalize_version_locked: copied WIP $ENT → published snapshot $LCK"

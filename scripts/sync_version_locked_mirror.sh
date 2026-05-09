#!/usr/bin/env bash
# Mirror version/entries into version/locked (exact copy for read-only visibility).
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
echo "sync_version_locked_mirror: updated $LCK from $ENT"

#!/usr/bin/env bash
# Finalize published versions: copy version/entries → version/locked (published snapshot).
# Top-level directories named "preproduction <A>.<B>.<C>/" under version/entries are
# develop-only and are NOT copied into version/locked (see docs/versioning.md).
# After this, run `make` or ./scripts/gen_version_def.sh so userland/shell/version_def.h
# reflects the highest A.B.C among version/locked/**/*.ver.
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
ENT="$ROOT/version/entries"
LCK="$ROOT/version/locked"
mkdir -p "$ENT" "$LCK"
find "$LCK" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
if [[ -d "$ENT" ]]; then
  shopt -s nullglob dotglob
  for item in "$ENT"/*; do
    [[ -e "$item" ]] || continue
    base=$(basename "$item")
    if [[ -d "$item" && "$base" == preproduction\ * ]]; then
      continue
    fi
    cp -a "$item" "$LCK"/
  done
  shopt -u nullglob dotglob
fi
echo "finalize_version_locked: copied $ENT → $LCK (skipped top-level preproduction */)"

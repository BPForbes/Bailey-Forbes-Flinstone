#!/usr/bin/env bash
# Finalize published versions: copy version/entries → version/locked (published snapshot).
# Top-level directories named "preproduction <A>.<B>.<C>/" under version/entries are
# develop-only and are NOT copied into version/locked (see docs/versioning.md).
# A *.ver row copied with RERELEASE=1 is written with RERELEASE=0 (scripts/lib/ver_locked_form.sh).
# After this, run `make` or ./scripts/gen_version_def.sh so userland/shell/version_def.h
# reflects the highest A.B.C among version/locked/**/*.ver.
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
# shellcheck source=lib/ver_locked_form.sh
source "$(cd "$(dirname "$0")" && pwd)/lib/ver_locked_form.sh"
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
  while IFS= read -r -d '' lf; do
    tmp="$lf.locked-form"
    ver_locked_form "$lf" >"$tmp"
    if cmp -s "$tmp" "$lf"; then
      rm -f "$tmp"
    else
      chmod --reference="$lf" "$tmp" 2>/dev/null || true
      mv -f "$tmp" "$lf"
    fi
  done < <(find "$LCK" -type f -name '*.ver' -print0)
  shopt -u nullglob dotglob
fi
echo "finalize_version_locked: copied $ENT → $LCK (skipped top-level preproduction */)"

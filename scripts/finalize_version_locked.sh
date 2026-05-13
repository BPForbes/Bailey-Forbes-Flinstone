#!/usr/bin/env bash
# Finalize published versions: copy version/entries → version/locked (published snapshot).
#
# Top-level directories named "preproduction <A>.<B>.<C>/" are develop-only: they are
# not copied into version/locked. Only root-level .ver files, ABOUT.txt, and other
# non-preproduction paths are published. After promote_preproduction_for_main.sh merges
# a preproduction folder into a single GA .ver at the entries root, the next finalize
# copies that file into locked like any other root entry.
#
# After this, run `make` or ./scripts/gen_version_def.sh so userland/shell/version_def.h
# reflects the highest A.B.C among version/locked/**/*.ver.
#
# Optional: REPO_ROOT=/path/to/repo to operate on a different tree (used by CI tests).
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
ENT="$ROOT/version/entries"
LCK="$ROOT/version/locked"
mkdir -p "$ENT" "$LCK"
find "$LCK" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
if [[ -d "$ENT" ]]; then
  while IFS= read -r -d '' item; do
    cp -a "$item" "$LCK"/
  done < <(find "$ENT" -mindepth 1 -maxdepth 1 ! -name 'preproduction *' -print0)
fi
echo "finalize_version_locked: copied $ENT → $LCK (excluded top-level preproduction */)"

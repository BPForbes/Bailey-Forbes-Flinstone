#!/usr/bin/env bash
# Stamp missing RELEASE_DATE=YYYY-MM-DD on:
#   - version/entries/*.ver at the entries repository root
#   - every *.ver under version/entries/preproduction <A>.<B>.<C>/ (active
#     preproduction trees present on the branch)
#
# GitHub Actions (prepare-version-entries) runs this after
# relocate_root_prerelease_ver_to_preproduction.sh so rows already under
# preproduction */ still receive RELEASE_DATE on the same job before
# gen_version_def.sh. gen_version_def.sh already reads version/entries/**/*.ver
# (including preproduction */) for VERSION_LINE and GM=1 overrides.
#
# Usage: ./scripts/stamp_version_entries_release_date.sh
# REPO_ROOT may point at a non-default repo root (tests).
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
ENT="$ROOT/version/entries"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_release_date_stamp.sh
source "$SCRIPT_DIR/lib/ver_release_date_stamp.sh"

d="$(date +%Y-%m-%d)"
stamped=0

while IFS= read -r -d '' f; do
  [[ -f "$f" ]] || continue
  rel="${f#"$ENT"/}"
  parent=$(dirname "$rel")
  if [[ "$parent" != "." && "$parent" != preproduction\ * ]]; then
    continue
  fi
  if ver_release_date_key_present "$f"; then
    continue
  fi
  ver_stamp_release_date_if_missing "$f" "$d"
  stamped=$((stamped + 1))
  echo "stamp_version_entries_release_date: $rel"
done < <(find "$ENT" -type f -name '*.ver' -print0 2>/dev/null)

if (( stamped == 0 )); then
  echo "stamp_version_entries_release_date: nothing to stamp (entries root and preproduction */ already dated)"
else
  echo "stamp_version_entries_release_date: stamped $stamped file(s) as $d"
fi

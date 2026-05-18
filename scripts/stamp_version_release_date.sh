#!/usr/bin/env bash
# Append RELEASE_DATE=YYYY-MM-DD to each *.ver under version/locked and
# version/entries when that key is absent. Intended to run after
# finalize_version_locked.sh (locked matches entries except develop-only
# top-level preproduction */ directories, which are not copied). Merge-time
# dates are recorded before gen_version_def.sh and changelog generation.
#
# Root PRERELEASE=1 rows get RELEASE_DATE on the relocate run (calendar day) just
# before they are moved under preproduction */ — see relocate_root_prerelease_ver_to_preproduction.sh.
# This pass still fills any remaining missing RELEASE_DATE keys.
# Recurses into subdirectories under version/locked, then stamps every *.ver under
# version/entries (including preproduction */ trees, which finalize does not copy
# into locked — without this second pass those rows would never get a merge-time date).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=lib/ver_release_date_stamp.sh
source "$(cd "$(dirname "$0")" && pwd)/lib/ver_release_date_stamp.sh"
LCK="$ROOT/version/locked"
ENT="$ROOT/version/entries"
d="$(date +%Y-%m-%d)"

while IFS= read -r -d '' f; do
  ver_stamp_release_date_if_missing "$f" "$d"
  rel="${f#"$LCK"/}"
  bf="$ENT/$rel"
  ver_stamp_release_date_if_missing "$bf" "$d"
done < <(find "$LCK" -type f -name '*.ver' -print0 2>/dev/null)

while IFS= read -r -d '' f; do
  ver_stamp_release_date_if_missing "$f" "$d"
done < <(find "$ENT" -type f -name '*.ver' -print0 2>/dev/null)

echo "stamp_version_release_date: stamped missing RELEASE_DATE (as $d) under $LCK and $ENT (recursive)"

#!/usr/bin/env bash
# After prepare-version-entries (relocate, normalize GM, promote, stamp, finalize),
# no preproduction */ directory may still contain GM=1. Otherwise gen_version_def.sh
# can surface A.B.C from prerelease while finalize leaves version/locked stale.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="check_version_gm1_preproduction_promoted"
err=0

while IFS= read -r -d '' dir; do
  while IFS= read -r -d '' vf; do
    [[ -f "$vf" ]] || continue
    gm_val=$(ver_parse_flag_field GM "$vf")
    if [[ "$gm_val" == "1" ]]; then
      echo "check_version_gm1_preproduction_promoted: GM=1 still in $vf — run promote in prepare-version-entries" >&2
      echo "  Expected: ./scripts/promote_preproduction_for_main.sh then ./scripts/finalize_version_locked.sh" >&2
      err=1
    fi
  done < <(find "$dir" -maxdepth 1 -type f -name '*.ver' -print0 2>/dev/null)
done < <(find "$ENT" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0 2>/dev/null)

if (( err )); then
  exit 1
fi
echo "check_version_gm1_preproduction_promoted: ok"

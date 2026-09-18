#!/usr/bin/env bash
# PUBLISHED_DESCRIPTION is the portfolio-facing override for DESCRIPTION, read
# only by promote_preproduction_for_main.sh when it promotes a GM=1 row. It may
# only be authored on that same GM=1 row: never on a routine root .ver (GM=1 is
# already forbidden there by check_version_prerelease_layout.sh) and never on a
# preproduction row that is not the GM=1 candidate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="check_version_published_description_requires_gm"
err=0

while IFS= read -r -d '' f; do
  if ver_field_has_key PUBLISHED_DESCRIPTION "$f"; then
    gm_val=$(ver_parse_flag_field GM "$f")
    if [[ "$gm_val" != "1" ]]; then
      echo "check_version_published_description_requires_gm: PUBLISHED_DESCRIPTION requires GM=1 on the same row — $f" >&2
      err=1
    fi
  fi
done < <(find "$ROOT/version/entries" "$ROOT/version/locked" -type f -name '*.ver' -print0 2>/dev/null)

if (( err )); then
  exit 1
fi
echo "check_version_published_description_requires_gm: ok"

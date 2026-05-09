#!/usr/bin/env bash
# Ensure userland/shell/version_def.h matches scripts/gen_version_def.sh output
# (i.e. the highest A.B.C among version/entries/*.ver).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEF="$ROOT/userland/shell/version_def.h"
GEN="$ROOT/scripts/gen_version_def.sh"

shopt -s nullglob
files=("$ROOT/version/entries"/*.ver)
if (( ${#files[@]} == 0 )); then
  echo "check_version_header_matches_entries: no .ver files; skipping"
  exit 0
fi

if [[ ! -f "$DEF" ]]; then
  echo "error: missing $DEF" >&2
  exit 1
fi

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
"$GEN" --stdout >"$tmp"
if ! cmp -s "$tmp" "$DEF"; then
  echo "error: $DEF is out of date relative to version/entries/*.ver" >&2
  echo "Run: ./scripts/gen_version_def.sh" >&2
  echo "Then commit the updated userland/shell/version_def.h" >&2
  exit 1
fi

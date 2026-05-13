#!/usr/bin/env bash
# Ensure userland/shell/version_def.h matches scripts/gen_version_def.sh output
# (highest A.B.C among version/locked/**/*.ver — finalized releases).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEF="$ROOT/userland/shell/version_def.h"
GEN="$ROOT/scripts/gen_version_def.sh"

if ! find "$ROOT/version/locked" -type f -name '*.ver' -print -quit 2>/dev/null | grep -q .; then
  echo "check_version_header_matches_locked: no version/locked/**/*.ver; skipping"
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
  echo "error: $DEF is out of date relative to version/locked/**/*.ver" >&2
  echo "Run: ./scripts/gen_version_def.sh (after finalize_version_locked.sh if needed)" >&2
  echo "Then commit the updated userland/shell/version_def.h" >&2
  exit 1
fi

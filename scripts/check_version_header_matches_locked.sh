#!/usr/bin/env bash
# Ensure userland/shell/version_def.h matches scripts/gen_version_def.sh output
# (shipped semver from version/locked/**/*.ver plus VERSION_LINE from version/entries/**/*.ver).
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
  echo "error: $DEF is out of date relative to gen_version_def.sh (locked + entries)" >&2
  echo "Run: ./scripts/gen_version_def.sh (after finalize_version_locked.sh if needed)" >&2
  echo "On same-repo feature branches, CI (versioning job in c-cpp.yml) regenerates and pushes this file after relocate; re-run checks or push that commit." >&2
  exit 1
fi

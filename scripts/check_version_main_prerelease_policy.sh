#!/usr/bin/env bash
# Main-line policy: no preproduction/* directories, no PRERELEASE=1, no GM=1, and no
# DEV_VERSION= lines in any .ver under version/entries or version/locked.
# Run scripts/promote_preproduction_for_main.sh before merging develop → main when promoting
# a prerelease line to GA.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
err=0

if find "$ROOT/version/entries" "$ROOT/version/locked" -mindepth 1 -type d -name 'preproduction *' -print -quit 2>/dev/null | grep -q .; then
  echo "check_version_main_prerelease_policy: preproduction */ directories must not exist on main." >&2
  echo "  Run: ./scripts/promote_preproduction_for_main.sh" >&2
  err=1
fi

while IFS= read -r -d '' f; do
  if grep -qE '^[[:space:]]*(int[[:space:]]+)?PRERELEASE=1([[:space:]]|$)' "$f"; then
    echo "check_version_main_prerelease_policy: PRERELEASE=1 not allowed in $f on main." >&2
    echo "  Run: ./scripts/promote_preproduction_for_main.sh" >&2
    err=1
  fi
  if grep -qE '^[[:space:]]*(int[[:space:]]+)?GM=1([[:space:]]|$)' "$f"; then
    echo "check_version_main_prerelease_policy: GM=1 not allowed in $f on main." >&2
    err=1
  fi
  if grep -qE '^[[:space:]]*(int[[:space:]]+)?DEV_VERSION=' "$f"; then
    echo "check_version_main_prerelease_policy: DEV_VERSION must not appear in $f on main." >&2
    err=1
  fi
done < <(find "$ROOT/version/entries" "$ROOT/version/locked" -type f -name '*.ver' -print0 2>/dev/null)

if (( err )); then
  exit 1
fi
echo "check_version_main_prerelease_policy: ok"

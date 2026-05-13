#!/usr/bin/env bash
# Fail if any *.ver under version/entries or version/locked declares DEV_PHASE.
# Intended for CI on main and for pull requests whose base branch is main:
# develop-phase D must not ship on the main line (see docs/versioning.md).
#
# Note: On a develop checkout that intentionally uses DEV_PHASE in entries,
# running this script locally will fail—that is expected. CI only invokes it
# for pushes to main and for PRs targeting main.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bad=0
for dir in "$ROOT/version/entries" "$ROOT/version/locked"; do
  [[ -d "$dir" ]] || continue
  shopt -s nullglob
  for f in "$dir"/*.ver; do
    if grep -qE '^[[:space:]]*(int[[:space:]]+)?DEV_PHASE=[[:space:]]*[0-9]+[[:space:]]*$' "$f"; then
      echo "check_version_dev_phase_absent_on_main: DEV_PHASE must not appear in $f on main." >&2
      echo "  Run: ./scripts/strip_dev_phase_from_ver_trees.sh" >&2
      bad=1
    fi
  done
done
if (( bad )); then
  exit 1
fi
echo "check_version_dev_phase_absent_on_main: ok (no DEV_PHASE= in version/*.ver)"

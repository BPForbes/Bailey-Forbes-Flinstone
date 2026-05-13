#!/usr/bin/env bash
# Remove DEV_PHASE= lines from every *.ver under version/entries and version/locked.
# Run before merging develop → main (or in the release PR into main) so main
# policy has no develop-phase key in published .ver files.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
changed=0
for dir in "$ROOT/version/entries" "$ROOT/version/locked"; do
  [[ -d "$dir" ]] || continue
  shopt -s nullglob
  for f in "$dir"/*.ver; do
    if grep -qE '^[[:space:]]*(int[[:space:]]+)?DEV_PHASE=[[:space:]]*[0-9]+[[:space:]]*$' "$f"; then
      tmp="${f}.tmp.$$"
      grep -vE '^[[:space:]]*(int[[:space:]]+)?DEV_PHASE=[[:space:]]*[0-9]+[[:space:]]*$' "$f" >"$tmp"
      mv "$tmp" "$f"
      echo "strip_dev_phase_from_ver_trees: removed DEV_PHASE from $f"
      changed=1
    fi
  done
done
if (( changed == 0 )); then
  echo "strip_dev_phase_from_ver_trees: no DEV_PHASE= lines found (nothing to do)"
fi

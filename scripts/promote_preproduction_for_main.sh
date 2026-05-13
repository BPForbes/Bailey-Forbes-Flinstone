#!/usr/bin/env bash
# Promote each version/entries (and version/locked) preproduction A.B.C/ directory
# to a single GA .ver at the tree root: pick the .ver with PRERELEASE=1 and the
# highest PRERELEASE_ITER, set PRERELEASE=0 (keep PRERELEASE_ITER as the final
# iteration number), write version/<root>/A_B_C_<same_basename>.ver, remove the
# preproduction directory.
#
# Run before merging version files into main (after CI layout checks on develop).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

promote_root() {
  local BASE="$1"
  [[ -d "$BASE" ]] || return 0
  while IFS= read -r -d '' dir; do
    local best="" best_iter=-1
    local vf
    shopt -s nullglob
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      grep -qE '^[[:space:]]*(int[[:space:]]+)?PRERELEASE=1([[:space:]]|$)' "$vf" || continue
      it=$(grep -E '^[[:space:]]*(int[[:space:]]+)?PRERELEASE_ITER=' "$vf" | head -1 |
        sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/')
      [[ -n "$it" ]] || continue
      if (( it > best_iter )); then
        best_iter=$it
        best=$vf
      elif (( it == best_iter )) && [[ -n "$best" ]]; then
        echo "promote_preproduction_for_main: ambiguous tie PRERELEASE_ITER=$it in $dir" >&2
        exit 1
      fi
    done
    if [[ -z "$best" ]]; then
      echo "promote_preproduction_for_main: no PRERELEASE=1 *.ver in $dir" >&2
      exit 1
    fi
    local dest="$BASE/$(basename "$best")"
    if [[ -e "$dest" ]]; then
      echo "promote_preproduction_for_main: refuse to overwrite existing $dest" >&2
      exit 1
    fi
    cp -a "$best" "$dest"
    sed -i -E 's/^([[:space:]]*(int[[:space:]]+)?PRERELEASE=)1/\10/' "$dest"
    rm -f "$dir"/*.ver
    rmdir "$dir"
    echo "promote_preproduction_for_main: promoted $(basename "$best") from $dir → $(basename "$dest") (PRERELEASE=0, PRERELEASE_ITER=$best_iter)"
  done < <(find "$BASE" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0)
}

promote_root "$ROOT/version/entries"
promote_root "$ROOT/version/locked"
echo "promote_preproduction_for_main: done"

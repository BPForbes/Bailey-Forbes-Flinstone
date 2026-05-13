#!/usr/bin/env bash
# Move version/entries/*.ver with PRERELEASE=1 from the entries root into
#   version/entries/preproduction <A>.<B>.<C>/
# using MAJOR/STANDARD/RELEASE (or alias keys) inside each file.
#
# For each such file, if RELEASE_DATE= is not yet set, appends RELEASE_DATE=YYYY-MM-DD
# (calendar day of this run) while the file is still at the root — before the move —
# so preproduction rows carry the relocation-day stamp. Merge-time
# stamp_version_release_date.sh still fills missing dates on other *.ver paths.
#
# Usage:
#   ./scripts/relocate_root_prerelease_ver_to_preproduction.sh
#   ./scripts/relocate_root_prerelease_ver_to_preproduction.sh --dry-run-exit-if-needed
#     (exit 1 if any root PRERELEASE=1 row exists; no edits — fork PR CI gate)
#
# REPO_ROOT may point at a non-default repo root (tests).
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
ENT="$ROOT/version/entries"
# shellcheck source=lib/ver_release_date_stamp.sh
source "$(cd "$(dirname "$0")" && pwd)/lib/ver_release_date_stamp.sh"

get_field() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/' || true
}

dry_gate=0
if [[ "${1:-}" == "--dry-run-exit-if-needed" ]]; then
  dry_gate=1
fi

shopt -s nullglob
ver_root=( "$ENT"/*.ver )
shopt -u nullglob

need_reloc=0
for f in "${ver_root[@]}"; do
  [[ -f "$f" ]] || continue
  rel="${f#"$ENT"/}"
  [[ "$(dirname "$rel")" == "." ]] || continue
  pr=$(get_field PRERELEASE "$f")
  [[ -n "$pr" ]] || pr=0
  if [[ "$pr" -eq 1 ]]; then
    need_reloc=1
    break
  fi
done

if (( dry_gate )); then
  if (( need_reloc )); then
    echo "relocate_root_prerelease_ver_to_preproduction: root PRERELEASE=1 .ver files must be relocated into preproduction */ but this environment cannot auto-push (fork PR). Run locally: ./scripts/relocate_root_prerelease_ver_to_preproduction.sh" >&2
    exit 1
  fi
  exit 0
fi

d="$(date +%Y-%m-%d)"
moved=0
for f in "${ver_root[@]}"; do
  [[ -f "$f" ]] || continue
  rel="${f#"$ENT"/}"
  [[ "$(dirname "$rel")" == "." ]] || continue
  pr=$(get_field PRERELEASE "$f")
  [[ -n "$pr" ]] || pr=0
  (( pr == 1 )) || continue

  m=$(get_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(get_field VERSION_MAJOR "$f")
  s=$(get_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(get_field VERSION_STANDARD "$f")
  r=$(get_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "relocate_root_prerelease_ver_to_preproduction: missing semver in $f" >&2
    exit 1
  fi

  ver_stamp_release_date_if_missing "$f" "$d"

  dest_dir="$ENT/preproduction ${m}.${s}.${r}"
  bn=$(basename "$f")
  dest="$dest_dir/$bn"
  mkdir -p "$dest_dir"
  if [[ -e "$dest" && "$f" != "$dest" ]]; then
    echo "relocate_root_prerelease_ver_to_preproduction: refuse overwrite — $dest already exists" >&2
    exit 1
  fi
  if [[ "$f" != "$dest" ]]; then
    mv "$f" "$dest"
    moved=1
    echo "relocate_root_prerelease_ver_to_preproduction: $rel -> preproduction ${m}.${s}.${r}/$bn"
  fi
done

if (( moved )); then
  echo "relocate_root_prerelease_ver_to_preproduction: done (moved at least one file)"
else
  echo "relocate_root_prerelease_ver_to_preproduction: nothing to do"
fi

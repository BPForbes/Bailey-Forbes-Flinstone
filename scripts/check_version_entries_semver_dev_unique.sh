#!/usr/bin/env bash
# Every *.ver under version/entries/ (including preproduction <A>.<B>.<C>/) must have a
# unique (MAJOR, STANDARD, RELEASE, DEV_VERSION) quadruple for the whole tree.
#
# O(kn): one linear pass over all entry .ver files (k top-level dirs × n files, plus
# preproduction trees); one sort + linear duplicate scan.
#
# DEV_VERSION omitted or non-numeric is treated as 0 for this key (GA-oriented rows).
# Authors: before assigning DEV_VERSION for a given A.B.C, scan version/entries and
# version/entries/preproduction A.B.C/ and pick the smallest unused non-negative int.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"

get_field() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/' || true
}

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

while IFS= read -r -d '' f; do
  m=$(get_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(get_field VERSION_MAJOR "$f")
  s=$(get_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(get_field VERSION_STANDARD "$f")
  r=$(get_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "check_version_entries_semver_dev_unique: missing semver in $f" >&2
    exit 1
  fi
  if ! [[ "$m" =~ ^[0-9]+$ && "$s" =~ ^[0-9]+$ && "$r" =~ ^[0-9]+$ ]]; then
    echo "check_version_entries_semver_dev_unique: non-integer semver component in $f" >&2
    exit 1
  fi
  dv_raw=$(grep -E "^[[:space:]]*(int[[:space:]]+)?DEV_VERSION=" "$f" 2>/dev/null | head -1 || true)
  d=0
  if [[ -n "$dv_raw" ]]; then
    dv=$(get_field DEV_VERSION "$f")
    if [[ -z "$dv" ]] || ! [[ "$dv" =~ ^[0-9]+$ ]]; then
      echo "check_version_entries_semver_dev_unique: DEV_VERSION must be a non-negative integer — $f" >&2
      exit 1
    fi
    d=$dv
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$m" "$s" "$r" "$d" "$f"
done < <(find "$ENT" -type f -name '*.ver' -print0 | sort -z) >"$tmp"

err=0
p1="" p2="" p3="" p4="" pf=""
while IFS=$'\t' read -r m s r d p; do
  [[ -n "$p" ]] || continue
  if [[ -n "$pf" && "$m" == "$p1" && "$s" == "$p2" && "$r" == "$p3" && "$d" == "$p4" ]]; then
    echo "check_version_entries_semver_dev_unique: duplicate (MAJOR=$m STANDARD=$s RELEASE=$r DEV_VERSION=$d):" >&2
    echo "  $pf" >&2
    echo "  $p" >&2
    err=1
  fi
  p1=$m p2=$s p3=$r p4=$d pf=$p
done < <(sort -t $'\t' -k1,1n -k2,2n -k3,3n -k4,4n -k5,5 "$tmp")

if (( err )); then
  exit 1
fi
echo "check_version_entries_semver_dev_unique: ok"

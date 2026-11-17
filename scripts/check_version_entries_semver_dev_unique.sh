#!/usr/bin/env bash
# Every *.ver under version/entries/ (including preproduction <A>.<B>.<C>/) must have a
# unique (MAJOR, STANDARD, RELEASE, DEV_VERSION) quadruple — except that RERELEASE=1
# allows reuse of an existing quadruple (same A.B.C + DEV_VERSION / BUILD) without
# bumping RELEASE_VERSION. In a colliding group, at most one row may lack RERELEASE=1
# (the original); every other row must set RERELEASE=1.
#
# O(kn): one linear pass over all entry .ver files (k top-level dirs × n files, plus
# preproduction trees); one sort + linear duplicate scan.
#
# DEV_VERSION omitted counts as 0 for this key (GA-oriented rows). When DEV_VERSION=
# is present, the value must be a non-negative integer (non-integer values exit 1).
# PRERELEASE, GM, and RERELEASE, when present, must be exactly 0 or 1 (binary flags).
# Semver fields must likewise be non-negative integers with no trailing junk (1abc fails).
# Authors: before assigning DEV_VERSION for a given A.B.C, default to the smallest unused
# non-negative int; set RERELEASE=1 only when intentionally reusing an existing BUILD.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="check_version_entries_semver_dev_unique"

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

while IFS= read -r -d '' f; do
  ver_parse_flag_field PRERELEASE "$f" >/dev/null
  ver_parse_flag_field GM "$f" >/dev/null
  rr=$(ver_parse_flag_field RERELEASE "$f")
  m=$(ver_parse_nonneg_int_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(ver_parse_nonneg_int_field VERSION_MAJOR "$f")
  s=$(ver_parse_nonneg_int_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(ver_parse_nonneg_int_field VERSION_STANDARD "$f")
  r=$(ver_parse_nonneg_int_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(ver_parse_nonneg_int_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(ver_parse_nonneg_int_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "check_version_entries_semver_dev_unique: missing semver in $f" >&2
    exit 1
  fi
  d=$(ver_parse_nonneg_int_field DEV_VERSION "$f")
  [[ -n "$d" ]] || d=0
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$s" "$r" "$d" "$rr" "$f"
done < <(find "$ENT" -type f -name '*.ver' -print0 | sort -z) >"$tmp"

err=0
# Flush a group of same (m,s,r,d) rows: allow when at most one row lacks RERELEASE=1.
flush_group() {
  local -a paths=()
  local -a rrs=()
  local i originals=0
  (( $# == 0 )) && return 0
  while (( $# >= 2 )); do
    paths+=("$1")
    rrs+=("$2")
    shift 2
  done
  (( ${#paths[@]} <= 1 )) && return 0
  for i in "${rrs[@]}"; do
    if [[ "$i" != "1" ]]; then
      originals=$((originals + 1))
    fi
  done
  if (( originals <= 1 )); then
    return 0
  fi
  echo "check_version_entries_semver_dev_unique: duplicate (MAJOR=$gm STANDARD=$gs RELEASE=$gr DEV_VERSION=$gd):" >&2
  for i in "${paths[@]}"; do
    echo "  $i" >&2
  done
  echo "  (Set RERELEASE=1 on add-on rows to reuse the same A.B.C + DEV_VERSION.)" >&2
  err=1
}

gm="" gs="" gr="" gd=""
group_args=()
while IFS=$'\t' read -r m s r d rr p; do
  [[ -n "$p" ]] || continue
  if [[ -n "$gm" && ( "$m" != "$gm" || "$s" != "$gs" || "$r" != "$gr" || "$d" != "$gd" ) ]]; then
    flush_group "${group_args[@]}"
    group_args=()
  fi
  gm=$m gs=$s gr=$r gd=$d
  group_args+=("$p" "$rr")
done < <(sort -t $'\t' -k1,1n -k2,2n -k3,3n -k4,4n -k6,6 "$tmp")
flush_group "${group_args[@]}"

if (( err )); then
  exit 1
fi
echo "check_version_entries_semver_dev_unique: ok"

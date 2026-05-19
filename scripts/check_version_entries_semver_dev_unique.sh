#!/usr/bin/env bash
# Every *.ver under version/entries/ (including preproduction <A>.<B>.<C>/) must have a
# unique (MAJOR, STANDARD, RELEASE, DEV_VERSION) quadruple for the whole tree.
#
# O(kn): one linear pass over all entry .ver files (k top-level dirs × n files, plus
# preproduction trees); one sort + linear duplicate scan.
#
# DEV_VERSION omitted counts as 0 for this key (GA-oriented rows). When DEV_VERSION=
# is present, the value must be a non-negative integer (non-integer values exit 1).
# PRERELEASE and GM, when present, must be exactly 0 or 1 (binary flags).
# Semver fields must likewise be non-negative integers with no trailing junk (1abc fails).
# Authors: before assigning DEV_VERSION for a given A.B.C, scan version/entries and
# version/entries/preproduction A.B.C/ and pick the smallest unused non-negative int.
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

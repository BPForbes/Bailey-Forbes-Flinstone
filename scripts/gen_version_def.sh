#!/usr/bin/env bash
# Emit userland/shell/version_def.h from:
#   - Shipped semver (VERSION_MAJOR/STANDARD/PATCH and VERSION): highest A.B.C among
#     version/locked/**/*.ver (recursive). PRERELEASE / GM / DEV_VERSION are ignored there.
#   - Display string VERSION_LINE: if any version/entries/**/*.ver has PRERELEASE=1,
#     the "newest" such row (highest semver, then highest DEV_VERSION) becomes
#     `PRERELEASE_TAG A.B.C` with optional ` BUILD-n` when DEV_VERSION>=1 (default tag PRE).
#     Otherwise VERSION_LINE matches VERSION (locked-only / no active prerelease in entries).
#
# Usage:
#   ./scripts/gen_version_def.sh           — write userland/shell/version_def.h
#   ./scripts/gen_version_def.sh --stdout  — print header to stdout (for CI diff)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LCK="$ROOT/version/locked"
ENT="$ROOT/version/entries"
DEF="$ROOT/userland/shell/version_def.h"

stdout=0
if [[ "${1:-}" == "--stdout" ]]; then
  stdout=1
fi

get_int_field() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/' || true
}

get_prerelease_tag_token() {
  local file="$1"
  local line val
  line=$(grep -E "^[[:space:]]*(int[[:space:]]+)?PRERELEASE_TAG=" "$file" 2>/dev/null | head -1 || true)
  [[ -z "$line" ]] && { echo -n "PRE"; return; }
  val="${line#*=}"
  val="${val#"${val%%[![:space:]]*}"}"
  val="${val%"${val##*[![:space:]]}"}"
  if [[ "${val:0:1}" == '"' && "${val: -1}" == '"' ]]; then
    val="${val:1:-1}"
  fi
  val="${val//[^A-Za-z0-9._+-]/}"
  [[ -n "$val" ]] || { echo -n "PRE"; return; }
  echo -n "$val"
}

c_string_escape() {
  local s=$1
  s=${s//\\/\\\\}
  s=${s//\"/\\\"}
  printf '%s' "$s"
}

bm=-1 bs=-1 br=-1
found=0
while IFS= read -r -d '' f; do
  found=1
  m=$(get_int_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(get_int_field VERSION_MAJOR "$f")
  s=$(get_int_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(get_int_field VERSION_STANDARD "$f")
  r=$(get_int_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_int_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_int_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "gen_version_def: could not parse semver from $f" >&2
    exit 1
  fi
  if (( bm < 0 )) || \
     (( m > bm )) || \
     (( m == bm && s > bs )) || \
     (( m == bm && s == bs && r > br )); then
    bm=$m bs=$s br=$r
  fi
done < <(find "$LCK" -type f -name '*.ver' -print0 2>/dev/null)

if (( found == 0 )); then
  echo "gen_version_def: no version/locked/**/*.ver files" >&2
  echo "Add work under version/entries/, run ./scripts/finalize_version_locked.sh, then retry." >&2
  exit 1
fi

# --- Prerelease display (version/entries only): highest (A,B,C), then highest DEV_VERSION
pm=-1 ps=-1 epr=-1
best_dv=-1
best_tag="PRE"
best_file=""
have_pr=0

while IFS= read -r -d '' f; do
  [[ -f "$f" ]] || continue
  pr=$(get_int_field PRERELEASE "$f")
  [[ -n "$pr" ]] || pr=0
  (( pr == 1 )) || continue

  m=$(get_int_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(get_int_field VERSION_MAJOR "$f")
  s=$(get_int_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(get_int_field VERSION_STANDARD "$f")
  r=$(get_int_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_int_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_int_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "gen_version_def: PRERELEASE=1 but missing semver in $f" >&2
    exit 1
  fi

  dv=$(get_int_field DEV_VERSION "$f")
  [[ -n "$dv" ]] || dv=0

  tag_tok=$(get_prerelease_tag_token "$f")

  replace=0
  if (( !have_pr )); then
    replace=1
  elif (( m > pm )) || \
       (( m == pm && s > ps )) || \
       (( m == pm && s == ps && r > epr )); then
    replace=1
  elif (( m == pm && s == ps && r == epr && dv > best_dv )); then
    replace=1
  fi

  if (( replace )); then
    have_pr=1
    pm=$m ps=$s epr=$r
    best_dv=$dv
    best_tag=$tag_tok
    best_file=$f
  fi
done < <(find "$ENT" -type f -name '*.ver' -print0 2>/dev/null)

shipped_line="${bm}.${bs}.${br}"
if (( !have_pr )); then
  display_line="$shipped_line"
else
  # Re-read tag from winning file so edits to that row win over parallel candidates
  if [[ -n "$best_file" && -f "$best_file" ]]; then
    best_tag=$(get_prerelease_tag_token "$best_file")
  fi
  display_line="${best_tag} ${pm}.${ps}.${epr}"
  if [[ -n "$best_dv" ]] && (( best_dv >= 1 )); then
    display_line+=" BUILD-${best_dv}"
  fi
fi

esc_line=$(c_string_escape "$display_line")

emit() {
  cat <<EOF
#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit by hand.
 *
 * Shipped numeric semver (VERSION_MAJOR/STANDARD/PATCH and VERSION) comes from the
 * highest A.B.C among all .ver files under version/locked/ (recursive). PRERELEASE,
 * GM, and DEV_VERSION there are ignored for those macros (see docs/versioning.md).
 *
 * VERSION_LINE is the shell-facing display string: when any .ver under version/entries/
 * has PRERELEASE=1, it shows PRERELEASE_TAG (default PRE), that row semver A.B.C, and
 * optional BUILD-n (n from DEV_VERSION) when DEV_VERSION>=1; otherwise VERSION_LINE
 * matches VERSION.
 *
 * To bump shipped semver: add new .ver files under version/entries/, finalize to
 * version/locked/, then run make or ./scripts/gen_version_def.sh.
 */
#define VERSION_MAJOR    ${bm}
#define VERSION_STANDARD ${bs}
#define VERSION_PATCH    ${br}

#define VERSION_STR_(x) #x
#define VERSION_STR(x) VERSION_STR_(x)

EOF
  printf '%s\n' '#define VERSION \'
  printf '%s\n' '    VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_STANDARD) "." VERSION_STR(VERSION_PATCH)'
  printf '%s\n' ''
  printf '#define VERSION_LINE "%s"\n' "$esc_line"
  printf '%s\n' ''
  printf '%s\n' '#endif /* VERSION_DEF_H */'
}

if [[ "$stdout" == 1 ]]; then
  emit
else
  mkdir -p "$(dirname "$DEF")"
  tmp="${DEF}.tmp.$$"
  emit >"$tmp"
  mv "$tmp" "$DEF"
  echo "gen_version_def: wrote $DEF (shipped ${bm}.${bs}.${br}; VERSION_LINE=${display_line})"
fi

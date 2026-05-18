#!/usr/bin/env bash
# Emit userland/shell/version_def.h from:
#   - Shipped semver (VERSION_MAJOR/STANDARD/PATCH and VERSION): highest A.B.C among
#     version/locked/**/*.ver (recursive). PRERELEASE / GM / DEV_VERSION are ignored there.
#   - Go-to-main override: if any version/entries/**/*.ver has PRERELEASE=1 and GM=1, the
#     "newest" such row (highest semver, then highest DEV_VERSION) supplies MAJOR/STANDARD/
#     RELEASE for VERSION_* / VERSION instead of the locked-only triple, and VERSION_LINE
#     is plain A.B.C (no PRERELEASE_TAG, no ", BUILD n").
#   - Otherwise, display string VERSION_LINE: if any version/entries/**/*.ver has PRERELEASE=1,
#     the "newest" such row becomes `PRERELEASE_TAG A.B.C` with optional `, BUILD n` when
#     DEV_VERSION>=1 (default tag PRE). Otherwise VERSION_LINE matches VERSION.
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

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="gen_version_def"

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

parse_semver_from_file() {
  # Sets shell vars: _m _s _r from first arg (.ver path); exit 1 if incomplete.
  local f="$1"
  _m=$(ver_parse_nonneg_int_field MAJOR_VERSION "$f")
  [[ -n "$_m" ]] || _m=$(ver_parse_nonneg_int_field VERSION_MAJOR "$f")
  _s=$(ver_parse_nonneg_int_field STANDARD_VERSION "$f")
  [[ -n "$_s" ]] || _s=$(ver_parse_nonneg_int_field VERSION_STANDARD "$f")
  _r=$(ver_parse_nonneg_int_field RELEASE_VERSION "$f")
  [[ -n "$_r" ]] || _r=$(ver_parse_nonneg_int_field MINOR_VERSION "$f")
  [[ -n "$_r" ]] || _r=$(ver_parse_nonneg_int_field VERSION_PATCH "$f")
  if [[ -z "$_m" || -z "$_s" || -z "$_r" ]]; then
    echo "gen_version_def: could not parse semver from $f" >&2
    exit 1
  fi
}

bm=-1 bs=-1 br=-1
found=0
while IFS= read -r -d '' f; do
  found=1
  parse_semver_from_file "$f"
  m=$_m s=$_s r=$_r
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

locked_bm=$bm locked_bs=$bs locked_br=$br

# --- GM=1 override (entries): shipped semver + plain VERSION_LINE from winning row
use_gm=0
gwm=-1 gws=-1 gwr=-1 gwdv=-1

while IFS= read -r -d '' f; do
  [[ -f "$f" ]] || continue
  pr=$(ver_parse_flag_field PRERELEASE "$f")
  (( pr == 1 )) || continue
  gm=$(ver_parse_flag_field GM "$f")
  (( gm == 1 )) || continue

  parse_semver_from_file "$f"
  m=$_m s=$_s r=$_r
  dv=$(ver_parse_nonneg_int_field DEV_VERSION "$f")
  [[ -n "$dv" ]] || dv=0

  replace=0
  if (( !use_gm )); then
    replace=1
  elif (( m > gwm )) || \
       (( m == gwm && s > gws )) || \
       (( m == gwm && s == gws && r > gwr )); then
    replace=1
  elif (( m == gwm && s == gws && r == gwr && dv > gwdv )); then
    replace=1
  fi

  if (( replace )); then
    use_gm=1
    gwm=$m gws=$s gwr=$r
    gwdv=$dv
  fi
done < <(find "$ENT" -type f -name '*.ver' -print0 2>/dev/null)

if (( use_gm )); then
  bm=$gwm bs=$gws br=$gwr
  display_line="${bm}.${bs}.${br}"
else
  bm=$locked_bm bs=$locked_bs br=$locked_br

  # --- Prerelease display (version/entries only): highest (A,B,C), then highest DEV_VERSION
  pm=-1 ps=-1 epr=-1
  best_dv=-1
  best_tag="PRE"
  best_file=""
  have_pr=0

  while IFS= read -r -d '' f; do
    [[ -f "$f" ]] || continue
    pr=$(ver_parse_flag_field PRERELEASE "$f")
    (( pr == 1 )) || continue

    parse_semver_from_file "$f"
    m=$_m s=$_s r=$_r

    dv=$(ver_parse_nonneg_int_field DEV_VERSION "$f")
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
    if [[ -n "$best_file" && -f "$best_file" ]]; then
      best_tag=$(get_prerelease_tag_token "$best_file")
    fi
    display_line="${best_tag} ${pm}.${ps}.${epr}"
    if [[ -n "$best_dv" ]] && (( best_dv >= 1 )); then
      display_line+=", BUILD ${best_dv}"
    fi
  fi
fi

esc_line=$(c_string_escape "$display_line")

emit() {
  cat <<EOF
#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit VERSION_* or VERSION_LINE by hand.
 *
 * Shipped numeric semver (VERSION_MAJOR/STANDARD/PATCH and VERSION): normally the highest
 * A.B.C among all .ver files under version/locked/ (recursive); PRERELEASE, GM, and
 * DEV_VERSION there are ignored for those macros.
 *
 * Go-to-main (entries): when any .ver under version/entries/ has PRERELEASE=1 and GM=1,
 * the newest such row (highest semver, then highest DEV_VERSION) supplies MAJOR/STANDARD/
 * RELEASE for VERSION_* / VERSION (RELEASE maps to VERSION_PATCH). VERSION_LINE is then
 * plain A.B.C with no prerelease tag and no ", BUILD n" suffix (for example "4.0.0").
 * GitHub Actions run this script and commit the refreshed header: .github/workflows/c-cpp.yml
 * (same-repo PR / feature-branch bot push after relocate), .github/workflows/version-lock-on-merge.yml
 * after pushes to develop (sync PR), and .github/actions/sync-version-checkout (build jobs).
 *
 * Otherwise VERSION_LINE follows version/entries PRERELEASE=1 rows: PRERELEASE_TAG
 * (default PRE), semver A.B.C, and optional ", BUILD n" when DEV_VERSION>=1; if no
 * PRERELEASE=1 rows exist, VERSION_LINE matches VERSION.
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

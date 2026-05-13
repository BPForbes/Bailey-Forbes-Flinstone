#!/usr/bin/env bash
# Emit userland/shell/version_def.h from the highest A.B.C among version/locked/**/*.ver
# (recursive). PRERELEASE / GM / DEV_VERSION are ignored for VERSION.
# Finalized releases live under version/locked (copied from version/entries via finalize_version_locked.sh,
# which omits top-level preproduction */ under version/entries).
#
# Usage:
#   ./scripts/gen_version_def.sh           — write userland/shell/version_def.h
#   ./scripts/gen_version_def.sh --stdout  — print header to stdout (for CI diff)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LCK="$ROOT/version/locked"
DEF="$ROOT/userland/shell/version_def.h"

stdout=0
if [[ "${1:-}" == "--stdout" ]]; then
  stdout=1
fi

bm=-1 bs=-1 br=-1
found=0
while IFS= read -r -d '' f; do
  found=1
  m=$(grep -E '^[[:space:]]*(int[[:space:]]+)?(MAJOR_VERSION|VERSION_MAJOR)=' "$f" | head -1 | sed -E 's/^[^=]*=([0-9]+).*/\1/')
  s=$(grep -E '^[[:space:]]*(int[[:space:]]+)?(STANDARD_VERSION|VERSION_STANDARD)=' "$f" | head -1 | sed -E 's/^[^=]*=([0-9]+).*/\1/')
  r=$(grep -E '^[[:space:]]*(int[[:space:]]+)?(RELEASE_VERSION|MINOR_VERSION|VERSION_PATCH)=' "$f" | head -1 | sed -E 's/^[^=]*=([0-9]+).*/\1/')
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

emit() {
  # Split the tail so we never put a lone `...` sequence in an unquoted heredoc (bash
  # would treat backticks as command substitution; CI runners then diverge from trees
  # committed with a hand-frozen header). VERSION uses explicit echoes for a stable \ continuation.
  cat <<EOF
#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit by hand.
 *
 * Built from finalized .ver files under version/locked/ by scripts/gen_version_def.sh (also run from the Makefile).
 * The shipped version is the highest A.B.C among those files (recursive tree; PRERELEASE / GM / DEV_VERSION are ignored for VERSION; see docs/versioning.md).
 *
 *   A (VERSION_MAJOR)     — milestones / architecture-scale changes
 *   B (VERSION_STANDARD)  — new features (semver "minor")
 *   C (VERSION_PATCH)     — fixes and small corrections (semver "patch")
 *
 * To bump: add version/entries/<A>_<B>_<C>_<slug>.ver (or under preproduction A.B.C/ while prerelease), run ./scripts/finalize_version_locked.sh, then run make or ./scripts/gen_version_def.sh.
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
  printf '%s\n' '#endif /* VERSION_DEF_H */'
}

if [[ "$stdout" == 1 ]]; then
  emit
else
  mkdir -p "$(dirname "$DEF")"
  tmp="${DEF}.tmp.$$"
  emit >"$tmp"
  mv "$tmp" "$DEF"
  echo "gen_version_def: wrote $DEF (${bm}.${bs}.${br})"
fi

#!/usr/bin/env bash
# Unit tests for PR-modified version scripts:
#   scripts/check_version_main_prerelease_policy.sh
#   scripts/check_version_prerelease_layout.sh
#   scripts/bump_dev_version.sh
#   scripts/lib/ver_release_date_stamp.sh
#   scripts/relocate_root_prerelease_ver_to_preproduction.sh
#
# Run from repository root: bash tests/test_version_scripts.sh
# Exit 0 on all pass, exit 1 on any failure.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASS=0
FAIL=0
SKIP=0

# Detect if bash process substitution is available (requires /dev/fd).
# Scripts under test use `< <(find ...)` which needs /dev/fd. When unavailable
# (sandboxed / container environments without /dev/fd mounted), skip those
# tests rather than falsely failing or passing.
HAS_PROC_SUB=0
if bash -c 'while read x; do :; done < <(echo probe)' 2>/dev/null; then
  HAS_PROC_SUB=1
fi

# ── helpers ────────────────────────────────────────────────────────────────────

ok() {
  PASS=$((PASS + 1))
  echo "PASS: $1"
}

fail() {
  FAIL=$((FAIL + 1))
  echo "FAIL: $1"
}

skip() {
  SKIP=$((SKIP + 1))
  echo "SKIP: $1 (requires /dev/fd — process substitution unavailable in this environment)"
}

# Guard: call at the top of any test that invokes a script using `< <(...)`.
# If process substitution is unavailable, skip the test (not a code defect).
require_proc_sub() {
  if (( HAS_PROC_SUB == 0 )); then
    skip "$1"
    return 1
  fi
  return 0
}

# Create a throwaway repo root with an isolated scripts/ directory so that
# scripts deriving ROOT via "$(dirname "$0")/.." see only the temp tree.
# Each script is copied (not symlinked) so ROOT resolves to $tmp, not to
# the real repository root.
#
# Usage: make_fake_repo script1 [script2 ...]
# Scripts are taken from REPO_ROOT/scripts/; lib/ is always included.
make_fake_repo() {
  local tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/scripts/lib" "$tmp/version/entries" "$tmp/version/locked" "$tmp/userland/shell"
  # Always copy the lib helper (sourced by relocate script)
  cp "$REPO_ROOT/scripts/lib/ver_release_date_stamp.sh" "$tmp/scripts/lib/"
  # Copy requested scripts
  local s
  for s in "$@"; do
    cp "$REPO_ROOT/scripts/$s" "$tmp/scripts/"
  done
  echo "$tmp"
}

cleanup() {
  local d="$1"
  rm -rf "$d"
}

# Make a minimal valid GA .ver file (no PRERELEASE, no GM, no DEV_VERSION)
write_ver_ga() {
  local f="$1" major="${2:-1}" std="${3:-0}" rel="${4:-0}"
  cat >"$f" <<VER
MAJOR_VERSION=${major}
STANDARD_VERSION=${std}
RELEASE_VERSION=${rel}
DESCRIPTION=Test GA release
VER
}

# Make a minimal valid PRERELEASE=1 .ver file with DEV_VERSION
write_ver_prerelease() {
  local f="$1" major="${2:-2}" std="${3:-0}" rel="${4:-0}" dv="${5:-1}"
  cat >"$f" <<VER
MAJOR_VERSION=${major}
STANDARD_VERSION=${std}
RELEASE_VERSION=${rel}
PRERELEASE=1
DEV_VERSION=${dv}
DESCRIPTION=Test prerelease
VER
}

# ── Tests: check_version_main_prerelease_policy.sh ────────────────────────────

# Script path in temp repo
main_policy_script() {
  local fake_root="$1"
  echo "$fake_root/scripts/check_version_main_prerelease_policy.sh"
}

test_main_policy_clean() {
  require_proc_sub "main_policy: clean GA tree passes" || return 0
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  # Create only GA .ver files (no PRERELEASE, GM, DEV_VERSION)
  write_ver_ga "$d/version/entries/1_0_0_feature.ver"
  write_ver_ga "$d/version/locked/1_0_0_feature.ver"

  if bash "$(main_policy_script "$d")" >/dev/null 2>&1; then
    ok "main_policy: clean GA tree passes"
  else
    fail "main_policy: clean GA tree should pass"
  fi
  cleanup "$d"
}

test_main_policy_empty_dirs() {
  require_proc_sub "main_policy: empty version dirs passes" || return 0
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  # Empty entries and locked — should still pass (no .ver to check)
  if bash "$(main_policy_script "$d")" >/dev/null 2>&1; then
    ok "main_policy: empty version dirs passes"
  else
    fail "main_policy: empty version dirs should pass"
  fi
  cleanup "$d"
}

test_main_policy_preproduction_dir_in_entries() {
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/dev.ver" 2 0 0 1

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: preproduction dir in entries should fail"
  else
    ok "main_policy: preproduction dir in entries is rejected"
  fi
  cleanup "$d"
}

test_main_policy_preproduction_dir_in_locked() {
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  mkdir -p "$d/version/locked/preproduction 3.0.0"
  write_ver_prerelease "$d/version/locked/preproduction 3.0.0/dev.ver" 3 0 0 1

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: preproduction dir in locked should fail"
  else
    ok "main_policy: preproduction dir in locked is rejected"
  fi
  cleanup "$d"
}

test_main_policy_prerelease_in_entries() {
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: PRERELEASE=1 in entries should fail"
  else
    ok "main_policy: PRERELEASE=1 in entries is rejected"
  fi
  cleanup "$d"
}

test_main_policy_prerelease_in_locked() {
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  write_ver_prerelease "$d/version/locked/2_0_0_pre.ver" 2 0 0 1

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: PRERELEASE=1 in locked should fail"
  else
    ok "main_policy: PRERELEASE=1 in locked is rejected"
  fi
  cleanup "$d"
}

test_main_policy_gm_in_entries() {
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  cat >"$d/version/entries/2_0_0_gm.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
GM=1
DESCRIPTION=GM release
VER

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: GM=1 in entries should fail"
  else
    ok "main_policy: GM=1 in entries is rejected"
  fi
  cleanup "$d"
}

test_main_policy_dev_version_in_locked() {
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  cat >"$d/version/locked/2_0_0_dev.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
DEV_VERSION=3
DESCRIPTION=Dev version
VER

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: DEV_VERSION in locked should fail"
  else
    ok "main_policy: DEV_VERSION in locked is rejected"
  fi
  cleanup "$d"
}

test_main_policy_dev_version_int_prefix() {
  # int DEV_VERSION=0 — must still be caught even with int prefix
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  cat >"$d/version/entries/2_0_1_dev.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=1
int DEV_VERSION=0
DESCRIPTION=Dev zero
VER

  if bash "$(main_policy_script "$d")" 2>/dev/null; then
    fail "main_policy: int DEV_VERSION= with int prefix should fail"
  else
    ok "main_policy: int DEV_VERSION= with int prefix is rejected"
  fi
  cleanup "$d"
}

test_main_policy_multiple_errors_reported() {
  require_proc_sub "main_policy: multiple errors all reported" || return 0
  local d
  d="$(make_fake_repo check_version_main_prerelease_policy.sh)"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1
  cat >"$d/version/locked/2_0_0_gm.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
GM=1
DESCRIPTION=GM
VER

  local out
  if out="$(bash "$(main_policy_script "$d")" 2>&1)"; then
    fail "main_policy: multiple errors should still fail"
  else
    # Both errors should appear in output
    if echo "$out" | grep -q "PRERELEASE=1" && echo "$out" | grep -q "GM=1"; then
      ok "main_policy: multiple errors all reported"
    else
      fail "main_policy: expected both PRERELEASE=1 and GM=1 errors in output, got: $out"
    fi
  fi
  cleanup "$d"
}

# ── Tests: check_version_prerelease_layout.sh ─────────────────────────────────

layout_script() {
  local fake_root="$1"
  echo "$fake_root/scripts/check_version_prerelease_layout.sh"
}

test_layout_empty_entries() {
  require_proc_sub "layout: empty entries directory passes" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: empty entries directory passes"
  else
    fail "layout: empty entries directory should pass"
  fi
  cleanup "$d"
}

test_layout_ga_root_ver() {
  require_proc_sub "layout: GA root .ver passes" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  write_ver_ga "$d/version/entries/1_0_0_feature.ver" 1 0 0

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: GA root .ver passes"
  else
    fail "layout: GA root .ver should pass"
  fi
  cleanup "$d"
}

test_layout_root_prerelease_with_dev_version() {
  require_proc_sub "layout: root PRERELEASE=1 with DEV_VERSION=1 passes" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: root PRERELEASE=1 with DEV_VERSION=1 passes"
  else
    fail "layout: root PRERELEASE=1 with DEV_VERSION=1 should pass"
  fi
  cleanup "$d"
}

test_layout_root_prerelease_without_dev_version() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  cat >"$d/version/entries/2_0_0_pre.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DESCRIPTION=Missing DEV_VERSION
VER

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: root PRERELEASE=1 without DEV_VERSION should fail"
  else
    ok "layout: root PRERELEASE=1 without DEV_VERSION is rejected"
  fi
  cleanup "$d"
}

test_layout_root_prerelease_dev_version_zero() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  cat >"$d/version/entries/2_0_0_pre.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DEV_VERSION=0
DESCRIPTION=DEV_VERSION is 0 but should be >=1
VER

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: root PRERELEASE=1 with DEV_VERSION=0 should fail"
  else
    ok "layout: root PRERELEASE=1 with DEV_VERSION=0 is rejected"
  fi
  cleanup "$d"
}

test_layout_root_gm_rejected() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  cat >"$d/version/entries/1_0_0_gm.ver" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
GM=1
DESCRIPTION=GM at root
VER

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: GM=1 at root should fail"
  else
    ok "layout: GM=1 at root is rejected"
  fi
  cleanup "$d"
}

test_layout_valid_preproduction_dir() {
  require_proc_sub "layout: valid preproduction dir with PRERELEASE=1 DEV_VERSION=1 passes" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/build1.ver" 2 0 0 1

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: valid preproduction dir with PRERELEASE=1 DEV_VERSION=1 passes"
  else
    fail "layout: valid preproduction dir should pass"
  fi
  cleanup "$d"
}

test_layout_preproduction_wrong_dir_name() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  # File says version 3.0.0 but dir says preproduction 2.0.0
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/mismatch.ver" 3 0 0 1

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: dir name mismatching semver should fail"
  else
    ok "layout: preproduction dir name must match .ver semver"
  fi
  cleanup "$d"
}

test_layout_preproduction_missing_prerelease() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  # GA .ver inside preproduction dir — PRERELEASE defaults to 0
  write_ver_ga "$d/version/entries/preproduction 2.0.0/ga.ver" 2 0 0

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: GA .ver inside preproduction dir should fail (PRERELEASE must be 1)"
  else
    ok "layout: GA .ver inside preproduction dir is rejected"
  fi
  cleanup "$d"
}

test_layout_preproduction_missing_dev_version() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  cat >"$d/version/entries/preproduction 2.0.0/nodev.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DESCRIPTION=No DEV_VERSION inside preproduction dir
VER

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: preproduction .ver without DEV_VERSION should fail"
  else
    ok "layout: preproduction .ver without DEV_VERSION is rejected"
  fi
  cleanup "$d"
}

test_layout_preproduction_gm_single_allowed() {
  require_proc_sub "layout: single GM=1 in preproduction dir passes" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  cat >"$d/version/entries/preproduction 2.0.0/build1.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=2
DESCRIPTION=GM release candidate
VER

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: single GM=1 in preproduction dir passes"
  else
    fail "layout: single GM=1 in preproduction dir should pass"
  fi
  cleanup "$d"
}

test_layout_preproduction_multiple_gm_rejected() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  cat >"$d/version/entries/preproduction 2.0.0/build1.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=1
DESCRIPTION=First GM
VER
  cat >"$d/version/entries/preproduction 2.0.0/build2.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=2
DESCRIPTION=Second GM not allowed
VER

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: two GM=1 in same preproduction dir should fail"
  else
    ok "layout: multiple GM=1 in same preproduction dir is rejected"
  fi
  cleanup "$d"
}

test_layout_multiple_preproduction_dirs() {
  require_proc_sub "layout: multiple valid preproduction dirs pass" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  mkdir -p "$d/version/entries/preproduction 3.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/a.ver" 2 0 0 1
  write_ver_prerelease "$d/version/entries/preproduction 3.0.0/b.ver" 3 0 0 1

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: multiple valid preproduction dirs pass"
  else
    fail "layout: multiple valid preproduction dirs should pass"
  fi
  cleanup "$d"
}

test_layout_missing_semver_rejected() {
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  # .ver file missing MAJOR_VERSION
  cat >"$d/version/entries/broken.ver" <<VER
STANDARD_VERSION=0
RELEASE_VERSION=0
DESCRIPTION=Missing MAJOR
VER

  if bash "$(layout_script "$d")" 2>/dev/null; then
    fail "layout: .ver missing semver fields should fail"
  else
    ok "layout: .ver missing semver fields is rejected"
  fi
  cleanup "$d"
}

test_layout_version_alias_keys_accepted() {
  # Alternative key names: VERSION_MAJOR, VERSION_STANDARD, VERSION_PATCH
  require_proc_sub "layout: alias semver keys (VERSION_MAJOR etc.) pass" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  cat >"$d/version/entries/1_2_3_alias.ver" <<VER
VERSION_MAJOR=1
VERSION_STANDARD=2
VERSION_PATCH=3
DESCRIPTION=Using alias keys
VER

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: alias semver keys (VERSION_MAJOR etc.) pass"
  else
    fail "layout: alias semver keys should be accepted"
  fi
  cleanup "$d"
}

test_layout_preproduction_dev_version_gte_2() {
  require_proc_sub "layout: preproduction .ver with DEV_VERSION=5 passes" || return 0
  local d
  d="$(make_fake_repo check_version_prerelease_layout.sh)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/build5.ver" 2 0 0 5

  if bash "$(layout_script "$d")" >/dev/null 2>&1; then
    ok "layout: preproduction .ver with DEV_VERSION=5 passes"
  else
    fail "layout: preproduction .ver with DEV_VERSION>=2 should pass"
  fi
  cleanup "$d"
}

# ── Tests: bump_dev_version.sh ────────────────────────────────────────────────

BUMP="$REPO_ROOT/scripts/bump_dev_version.sh"

test_bump_increments_dev_version() {
  local f
  f="$(mktemp /tmp/bump_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
DEV_VERSION=3
DESCRIPTION=Bump test
VER

  bash "$BUMP" "$f" >/dev/null
  local got
  got="$(grep '^DEV_VERSION=' "$f" | head -1 | sed 's/[^0-9]//g')"
  if [[ "$got" == "4" ]]; then
    ok "bump: DEV_VERSION incremented from 3 to 4"
  else
    fail "bump: expected DEV_VERSION=4, got '$got'"
  fi
  rm -f "$f"
}

test_bump_from_one() {
  local f
  f="$(mktemp /tmp/bump_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
DEV_VERSION=1
DESCRIPTION=Bump from 1
VER

  bash "$BUMP" "$f" >/dev/null
  local got
  got="$(grep '^DEV_VERSION=' "$f" | head -1 | sed 's/[^0-9]//g')"
  if [[ "$got" == "2" ]]; then
    ok "bump: DEV_VERSION incremented from 1 to 2"
  else
    fail "bump: expected DEV_VERSION=2, got '$got'"
  fi
  rm -f "$f"
}

test_bump_from_zero() {
  local f
  f="$(mktemp /tmp/bump_test_XXXX.ver)"
  cat >"$f" <<VER
DEV_VERSION=0
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
VER

  bash "$BUMP" "$f" >/dev/null
  local got
  got="$(grep '^DEV_VERSION=' "$f" | head -1 | sed 's/[^0-9]//g')"
  if [[ "$got" == "1" ]]; then
    ok "bump: DEV_VERSION incremented from 0 to 1"
  else
    fail "bump: expected DEV_VERSION=1, got '$got'"
  fi
  rm -f "$f"
}

test_bump_int_prefixed_key() {
  # bump_dev_version.sh must handle "int DEV_VERSION=N" syntax
  local f
  f="$(mktemp /tmp/bump_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=2
int DEV_VERSION=7
DESCRIPTION=Int prefix style
VER

  bash "$BUMP" "$f" >/dev/null
  local got
  got="$(grep -E 'DEV_VERSION=' "$f" | head -1 | sed 's/[^0-9]//g')"
  if [[ "$got" == "8" ]]; then
    ok "bump: int DEV_VERSION=7 incremented to 8"
  else
    fail "bump: expected 8 after bumping int DEV_VERSION=7, got '$got'"
  fi
  rm -f "$f"
}

test_bump_missing_dev_version_key_fails() {
  local f
  f="$(mktemp /tmp/bump_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
DESCRIPTION=No DEV_VERSION
VER

  if bash "$BUMP" "$f" 2>/dev/null; then
    fail "bump: missing DEV_VERSION should fail"
  else
    ok "bump: missing DEV_VERSION exits non-zero"
  fi
  rm -f "$f"
}

test_bump_nonexistent_file_fails() {
  if bash "$BUMP" "/tmp/nonexistent_bump_test_file_$$.ver" 2>/dev/null; then
    fail "bump: non-existent file should fail"
  else
    ok "bump: non-existent file exits non-zero"
  fi
}

test_bump_no_args_fails() {
  if bash "$BUMP" 2>/dev/null; then
    fail "bump: no arguments should fail"
  else
    ok "bump: no arguments exits non-zero"
  fi
}

test_bump_preserves_other_fields() {
  local f
  f="$(mktemp /tmp/bump_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=1
RELEASE_VERSION=2
PRERELEASE=1
DEV_VERSION=4
DESCRIPTION=Preserve other fields
RELEASE_DATE=2025-01-01
VER

  bash "$BUMP" "$f" >/dev/null
  local major prerelease desc dv
  major="$(grep '^MAJOR_VERSION=' "$f" | sed 's/[^0-9]//g')"
  prerelease="$(grep '^PRERELEASE=' "$f" | sed 's/[^0-9]//g')"
  desc="$(grep '^DESCRIPTION=' "$f" | sed 's/^DESCRIPTION=//')"
  dv="$(grep '^DEV_VERSION=' "$f" | sed 's/[^0-9]//g')"

  if [[ "$major" == "3" && "$prerelease" == "1" && "$dv" == "5" && "$desc" == "Preserve other fields" ]]; then
    ok "bump: other fields preserved after bump"
  else
    fail "bump: fields changed unexpectedly (major=$major prerelease=$prerelease dv=$dv desc=$desc)"
  fi
  rm -f "$f"
}

# ── Tests: scripts/lib/ver_release_date_stamp.sh ─────────────────────────────

LIB_STAMP="$REPO_ROOT/scripts/lib/ver_release_date_stamp.sh"

# Source the library for direct function testing
# shellcheck source=/dev/null
source "$LIB_STAMP"

test_stamp_key_absent() {
  local f
  f="$(mktemp /tmp/stamp_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
DESCRIPTION=No RELEASE_DATE
VER

  if ver_release_date_key_present "$f"; then
    fail "stamp: RELEASE_DATE absent — ver_release_date_key_present should return 1"
  else
    ok "stamp: ver_release_date_key_present returns false when key absent"
  fi
  rm -f "$f"
}

test_stamp_key_present() {
  local f
  f="$(mktemp /tmp/stamp_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
DESCRIPTION=With date
RELEASE_DATE=2025-03-15
VER

  if ver_release_date_key_present "$f"; then
    ok "stamp: ver_release_date_key_present returns true when key present"
  else
    fail "stamp: RELEASE_DATE present — should return true"
  fi
  rm -f "$f"
}

test_stamp_appends_when_missing() {
  local f
  f="$(mktemp /tmp/stamp_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
DESCRIPTION=Stamp me
VER

  ver_stamp_release_date_if_missing "$f" "2025-06-01"
  if grep -q "^RELEASE_DATE=2025-06-01" "$f"; then
    ok "stamp: RELEASE_DATE appended when missing"
  else
    fail "stamp: RELEASE_DATE not appended"
  fi
  rm -f "$f"
}

test_stamp_does_not_duplicate() {
  local f
  f="$(mktemp /tmp/stamp_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
RELEASE_DATE=2025-01-01
DESCRIPTION=Already dated
VER

  ver_stamp_release_date_if_missing "$f" "2025-09-01"
  local count
  count="$(grep -c '^RELEASE_DATE=' "$f")"
  if [[ "$count" == "1" ]]; then
    ok "stamp: RELEASE_DATE not duplicated when already present"
  else
    fail "stamp: expected 1 RELEASE_DATE line, found $count"
  fi
  rm -f "$f"
}

test_stamp_does_not_overwrite_existing_date() {
  local f
  f="$(mktemp /tmp/stamp_test_XXXX.ver)"
  cat >"$f" <<VER
RELEASE_DATE=2024-12-31
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
VER

  ver_stamp_release_date_if_missing "$f" "2025-09-01"
  if grep -q "^RELEASE_DATE=2024-12-31" "$f"; then
    ok "stamp: existing RELEASE_DATE not overwritten"
  else
    fail "stamp: existing RELEASE_DATE was changed or removed"
  fi
  rm -f "$f"
}

test_stamp_key_in_heredoc_not_confused() {
  # RELEASE_DATE inside a heredoc body should NOT be treated as the key
  local f
  f="$(mktemp /tmp/stamp_test_XXXX.ver)"
  cat >"$f" <<VER
MAJOR_VERSION=1
STANDARD_VERSION=0
RELEASE_VERSION=0
DESCRIPTION<<END
Changes since last release:
- RELEASE_DATE=not a real key
- Fixed things
END
VER

  if ver_release_date_key_present "$f"; then
    fail "stamp: RELEASE_DATE inside heredoc body should NOT be treated as the key"
  else
    ok "stamp: RELEASE_DATE inside heredoc body is ignored correctly"
  fi
  rm -f "$f"
}

test_stamp_nonexistent_file_safe() {
  # ver_stamp_release_date_if_missing returns 0 for nonexistent file
  if ver_stamp_release_date_if_missing "/tmp/nonexistent_stamp_test_$$.ver" "2025-01-01"; then
    ok "stamp: nonexistent file is handled gracefully (returns 0)"
  else
    fail "stamp: nonexistent file caused non-zero return"
  fi
}

# ── Tests: relocate_root_prerelease_ver_to_preproduction.sh ───────────────────

RELOCATE="$REPO_ROOT/scripts/relocate_root_prerelease_ver_to_preproduction.sh"

test_relocate_no_prerelease_noop() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_ga "$d/version/entries/1_0_0_ga.ver" 1 0 0

  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  # File should remain at root (not moved)
  if [[ -f "$d/version/entries/1_0_0_ga.ver" ]]; then
    ok "relocate: GA .ver not moved (no PRERELEASE=1)"
  else
    fail "relocate: GA .ver was incorrectly moved"
  fi
  rm -rf "$d"
}

test_relocate_moves_prerelease_ver() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1

  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  # File should have moved to preproduction 2.0.0/
  if [[ -f "$d/version/entries/preproduction 2.0.0/2_0_0_pre.ver" ]]; then
    ok "relocate: PRERELEASE=1 .ver moved to preproduction 2.0.0/"
  else
    fail "relocate: PRERELEASE=1 .ver not moved correctly"
  fi
  # Original should not exist
  if [[ ! -f "$d/version/entries/2_0_0_pre.ver" ]]; then
    ok "relocate: original PRERELEASE=1 .ver removed from root"
  else
    fail "relocate: original PRERELEASE=1 .ver still at root after relocate"
  fi
  rm -rf "$d"
}

test_relocate_stamps_release_date() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1

  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  local dest="$d/version/entries/preproduction 2.0.0/2_0_0_pre.ver"
  if [[ -f "$dest" ]] && grep -q '^RELEASE_DATE=' "$dest"; then
    ok "relocate: RELEASE_DATE stamped on moved PRERELEASE=1 .ver"
  else
    fail "relocate: RELEASE_DATE not stamped after relocate"
  fi
  rm -rf "$d"
}

test_relocate_does_not_stamp_existing_release_date() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  cat >"$d/version/entries/2_0_0_pre.ver" <<VER
MAJOR_VERSION=2
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DEV_VERSION=1
RELEASE_DATE=2024-01-01
DESCRIPTION=Pre with date
VER

  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  local dest="$d/version/entries/preproduction 2.0.0/2_0_0_pre.ver"
  local count
  count="$(grep -c '^RELEASE_DATE=' "$dest" 2>/dev/null || echo 0)"
  if [[ "$count" == "1" ]] && grep -q '^RELEASE_DATE=2024-01-01' "$dest"; then
    ok "relocate: pre-existing RELEASE_DATE not overwritten or duplicated"
  else
    fail "relocate: RELEASE_DATE changed (count=$count)"
  fi
  rm -rf "$d"
}

test_relocate_dry_run_exits_when_prerelease_at_root() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1

  if REPO_ROOT="$d" bash "$RELOCATE" --dry-run-exit-if-needed 2>/dev/null; then
    fail "relocate --dry-run-exit-if-needed: should exit 1 when PRERELEASE=1 root .ver exists"
  else
    ok "relocate --dry-run-exit-if-needed: exits non-zero when PRERELEASE=1 root .ver present"
  fi
  # File must not have been moved
  if [[ -f "$d/version/entries/2_0_0_pre.ver" ]]; then
    ok "relocate --dry-run-exit-if-needed: file not moved in dry-run mode"
  else
    fail "relocate --dry-run-exit-if-needed: file was moved despite dry-run"
  fi
  rm -rf "$d"
}

test_relocate_dry_run_exits_zero_when_clean() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_ga "$d/version/entries/1_0_0_ga.ver" 1 0 0

  if REPO_ROOT="$d" bash "$RELOCATE" --dry-run-exit-if-needed 2>/dev/null; then
    ok "relocate --dry-run-exit-if-needed: exits 0 when no root PRERELEASE=1 .ver"
  else
    fail "relocate --dry-run-exit-if-needed: should exit 0 on clean tree"
  fi
  rm -rf "$d"
}

test_relocate_idempotent() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1

  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  # Run again — should be a no-op (already relocated)
  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  local count
  count="$(find "$d/version/entries/preproduction 2.0.0" -name '*.ver' | wc -l | tr -d ' ')"
  if [[ "$count" == "1" ]]; then
    ok "relocate: idempotent — second run does not duplicate files"
  else
    fail "relocate: expected 1 file after second run, found $count"
  fi
  rm -rf "$d"
}

test_relocate_refuses_overwrite() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 2.0.0"
  # Conflict: root file and existing preproduction file with the same name
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/2_0_0_pre.ver" 2 0 0 2

  if REPO_ROOT="$d" bash "$RELOCATE" 2>/dev/null; then
    fail "relocate: should refuse to overwrite existing file in preproduction dir"
  else
    ok "relocate: refuses to overwrite existing file in preproduction dir"
  fi
  rm -rf "$d"
}

# Boundary case: multiple root PRERELEASE=1 files with different semvers
test_relocate_multiple_prerelease_files() {
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries"
  write_ver_prerelease "$d/version/entries/2_0_0_pre.ver" 2 0 0 1
  write_ver_prerelease "$d/version/entries/3_0_0_pre.ver" 3 0 0 1

  REPO_ROOT="$d" bash "$RELOCATE" >/dev/null
  if [[ -f "$d/version/entries/preproduction 2.0.0/2_0_0_pre.ver" && \
        -f "$d/version/entries/preproduction 3.0.0/3_0_0_pre.ver" ]]; then
    ok "relocate: multiple PRERELEASE=1 files relocated to separate preproduction dirs"
  else
    fail "relocate: not all PRERELEASE=1 files relocated correctly"
  fi
  rm -rf "$d"
}

# ── Run all tests ─────────────────────────────────────────────────────────────

# check_version_main_prerelease_policy.sh
test_main_policy_clean
test_main_policy_empty_dirs
test_main_policy_preproduction_dir_in_entries
test_main_policy_preproduction_dir_in_locked
test_main_policy_prerelease_in_entries
test_main_policy_prerelease_in_locked
test_main_policy_gm_in_entries
test_main_policy_dev_version_in_locked
test_main_policy_dev_version_int_prefix
test_main_policy_multiple_errors_reported

# check_version_prerelease_layout.sh
test_layout_empty_entries
test_layout_ga_root_ver
test_layout_root_prerelease_with_dev_version
test_layout_root_prerelease_without_dev_version
test_layout_root_prerelease_dev_version_zero
test_layout_root_gm_rejected
test_layout_valid_preproduction_dir
test_layout_preproduction_wrong_dir_name
test_layout_preproduction_missing_prerelease
test_layout_preproduction_missing_dev_version
test_layout_preproduction_gm_single_allowed
test_layout_preproduction_multiple_gm_rejected
test_layout_multiple_preproduction_dirs
test_layout_missing_semver_rejected
test_layout_version_alias_keys_accepted
test_layout_preproduction_dev_version_gte_2

# bump_dev_version.sh
test_bump_increments_dev_version
test_bump_from_one
test_bump_from_zero
test_bump_int_prefixed_key
test_bump_missing_dev_version_key_fails
test_bump_nonexistent_file_fails
test_bump_no_args_fails
test_bump_preserves_other_fields

# scripts/lib/ver_release_date_stamp.sh
test_stamp_key_absent
test_stamp_key_present
test_stamp_appends_when_missing
test_stamp_does_not_duplicate
test_stamp_does_not_overwrite_existing_date
test_stamp_key_in_heredoc_not_confused
test_stamp_nonexistent_file_safe

# relocate_root_prerelease_ver_to_preproduction.sh
test_relocate_no_prerelease_noop
test_relocate_moves_prerelease_ver
test_relocate_stamps_release_date
test_relocate_does_not_stamp_existing_release_date
test_relocate_dry_run_exits_when_prerelease_at_root
test_relocate_dry_run_exits_zero_when_clean
test_relocate_idempotent
test_relocate_refuses_overwrite
test_relocate_multiple_prerelease_files

# ── Tests: gen_version_def.sh — VERSION_LINE prerelease behavior ──────────────

GEN_VER="$REPO_ROOT/scripts/gen_version_def.sh"
FINALIZE="$REPO_ROOT/scripts/finalize_version_locked.sh"
PROMOTE="$REPO_ROOT/scripts/promote_preproduction_for_main.sh"

# Make a minimal locked .ver (GA) and entries .ver (also GA), then run gen_version_def.sh
# in --stdout mode; VERSION_LINE must equal the VERSION string when no PRERELEASE=1 in entries.
test_gen_version_def_no_prerelease_version_line_matches() {
  require_proc_sub "gen_version_def: no PRERELEASE → VERSION_LINE == VERSION" || return 0
  local d
  d="$(make_fake_repo gen_version_def.sh)"
  write_ver_ga "$d/version/locked/2_1_3_release.ver" 2 1 3
  write_ver_ga "$d/version/entries/2_1_3_release.ver" 2 1 3
  touch "$d/userland/shell/version_def.h"

  local out
  if ! out="$(REPO_ROOT="$d" bash "$d/scripts/gen_version_def.sh" --stdout 2>&1)"; then
    fail "gen_version_def: GA-only tree — script failed unexpectedly"
    cleanup "$d"
    return 0
  fi
  local ver_line
  ver_line="$(echo "$out" | grep '^#define VERSION_LINE' | sed 's/#define VERSION_LINE "//' | sed 's/"//')"
  if [[ "$ver_line" == "2.1.3" ]]; then
    ok "gen_version_def: no PRERELEASE → VERSION_LINE == VERSION (2.1.3)"
  else
    fail "gen_version_def: expected VERSION_LINE=\"2.1.3\", got \"$ver_line\""
  fi
  cleanup "$d"
}

# When entries has a PRERELEASE=1 file under preproduction */, VERSION_LINE must show
# the prerelease tag and semver rather than the locked GA version.
test_gen_version_def_prerelease_version_line_shown() {
  require_proc_sub "gen_version_def: PRERELEASE=1 in entries → VERSION_LINE shows tag+semver" || return 0
  local d
  d="$(make_fake_repo gen_version_def.sh)"
  # Locked has 2.1.3 (GA) — this determines VERSION / VERSION_*
  write_ver_ga "$d/version/locked/2_1_3_release.ver" 2 1 3
  # Entries has a preproduction dir with PRERELEASE=1 at 3.0.0
  mkdir -p "$d/version/entries/preproduction 3.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 3.0.0/build1.ver" 3 0 0 1
  touch "$d/userland/shell/version_def.h"

  local out
  if ! out="$(REPO_ROOT="$d" bash "$d/scripts/gen_version_def.sh" --stdout 2>&1)"; then
    fail "gen_version_def: PRERELEASE in entries — script failed"
    cleanup "$d"
    return 0
  fi
  local ver_line
  ver_line="$(echo "$out" | grep '^#define VERSION_LINE' | sed 's/#define VERSION_LINE "//' | sed 's/"//')"
  # VERSION_LINE must contain the semver 3.0.0 (the prerelease version)
  if echo "$ver_line" | grep -q "3.0.0"; then
    ok "gen_version_def: PRERELEASE=1 → VERSION_LINE contains 3.0.0"
  else
    fail "gen_version_def: expected VERSION_LINE to contain 3.0.0, got \"$ver_line\""
  fi
  # VERSION (numeric macros) must still be the locked GA version 2.1.3
  if echo "$out" | grep -q '#define VERSION_MAJOR *2$'; then
    ok "gen_version_def: VERSION_MAJOR still reflects locked GA (2)"
  else
    fail "gen_version_def: expected VERSION_MAJOR=2 from locked, output was: $(echo "$out" | grep VERSION_MAJOR)"
  fi
  cleanup "$d"
}

# When PRERELEASE=1 row has DEV_VERSION >= 1, VERSION_LINE includes ", BUILD N".
test_gen_version_def_dev_version_in_version_line() {
  require_proc_sub "gen_version_def: DEV_VERSION>=1 → VERSION_LINE includes BUILD N" || return 0
  local d
  d="$(make_fake_repo gen_version_def.sh)"
  write_ver_ga "$d/version/locked/2_1_3_release.ver" 2 1 3
  mkdir -p "$d/version/entries/preproduction 3.0.0"
  cat >"$d/version/entries/preproduction 3.0.0/build5.ver" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DEV_VERSION=5
DESCRIPTION=Build 5
VER
  touch "$d/userland/shell/version_def.h"

  local out
  if ! out="$(REPO_ROOT="$d" bash "$d/scripts/gen_version_def.sh" --stdout 2>&1)"; then
    fail "gen_version_def: DEV_VERSION>=1 case — script failed"
    cleanup "$d"
    return 0
  fi
  local ver_line
  ver_line="$(echo "$out" | grep '^#define VERSION_LINE' | sed 's/#define VERSION_LINE "//' | sed 's/"//')"
  if echo "$ver_line" | grep -q "BUILD 5"; then
    ok "gen_version_def: DEV_VERSION=5 → VERSION_LINE includes \", BUILD 5\""
  else
    fail "gen_version_def: expected VERSION_LINE to include 'BUILD 5', got \"$ver_line\""
  fi
  cleanup "$d"
}

# Multiple PRERELEASE=1 rows: VERSION_LINE must pick the highest semver, then highest DEV_VERSION.
test_gen_version_def_picks_highest_prerelease() {
  require_proc_sub "gen_version_def: picks highest semver prerelease for VERSION_LINE" || return 0
  local d
  d="$(make_fake_repo gen_version_def.sh)"
  write_ver_ga "$d/version/locked/2_1_3_release.ver" 2 1 3
  mkdir -p "$d/version/entries/preproduction 3.0.0"
  mkdir -p "$d/version/entries/preproduction 4.0.0"
  write_ver_prerelease "$d/version/entries/preproduction 3.0.0/a.ver" 3 0 0 1
  write_ver_prerelease "$d/version/entries/preproduction 4.0.0/b.ver" 4 0 0 2
  touch "$d/userland/shell/version_def.h"

  local out
  if ! out="$(REPO_ROOT="$d" bash "$d/scripts/gen_version_def.sh" --stdout 2>&1)"; then
    fail "gen_version_def: multiple prerelease dirs — script failed"
    cleanup "$d"
    return 0
  fi
  local ver_line
  ver_line="$(echo "$out" | grep '^#define VERSION_LINE' | sed 's/#define VERSION_LINE "//' | sed 's/"//')"
  if echo "$ver_line" | grep -q "4.0.0"; then
    ok "gen_version_def: highest semver prerelease (4.0.0) wins for VERSION_LINE"
  else
    fail "gen_version_def: expected 4.0.0 in VERSION_LINE, got \"$ver_line\""
  fi
  cleanup "$d"
}

# gen_version_def.sh fails when there are no locked .ver files (no shipped semver).
test_gen_version_def_fails_no_locked_vers() {
  require_proc_sub "gen_version_def: no locked .ver exits non-zero" || return 0
  local d
  d="$(make_fake_repo gen_version_def.sh)"
  # No locked .ver files; entries has one but gen reads locked for VERSION
  write_ver_ga "$d/version/entries/1_0_0_ga.ver" 1 0 0
  touch "$d/userland/shell/version_def.h"

  if REPO_ROOT="$d" bash "$d/scripts/gen_version_def.sh" --stdout >/dev/null 2>&1; then
    fail "gen_version_def: no locked .ver should fail"
  else
    ok "gen_version_def: exits non-zero when no locked .ver files"
  fi
  cleanup "$d"
}

# ── Tests: finalize_version_locked.sh — preproduction */ exclusion ────────────

test_finalize_excludes_preproduction_dirs() {
  require_proc_sub "finalize: preproduction */ dirs not copied to locked" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 3.0.0" "$d/version/locked"
  write_ver_ga "$d/version/entries/2_0_0_ga.ver" 2 0 0
  write_ver_prerelease "$d/version/entries/preproduction 3.0.0/pre.ver" 3 0 0 1

  REPO_ROOT="$d" bash "$FINALIZE" >/dev/null

  # The GA .ver must be in locked
  if [[ -f "$d/version/locked/2_0_0_ga.ver" ]]; then
    ok "finalize: GA .ver copied to locked"
  else
    fail "finalize: GA .ver missing from locked"
  fi
  # The preproduction dir must NOT be in locked
  if [[ ! -d "$d/version/locked/preproduction 3.0.0" ]]; then
    ok "finalize: preproduction 3.0.0/ NOT copied to locked"
  else
    fail "finalize: preproduction 3.0.0/ was incorrectly copied to locked"
  fi
  rm -rf "$d"
}

test_finalize_copies_non_preproduction_files() {
  require_proc_sub "finalize: non-preproduction .ver files are copied to locked" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries" "$d/version/locked"
  write_ver_ga "$d/version/entries/1_2_0_feature.ver" 1 2 0
  write_ver_ga "$d/version/entries/1_2_1_patch.ver"   1 2 1

  REPO_ROOT="$d" bash "$FINALIZE" >/dev/null

  local count
  count="$(find "$d/version/locked" -maxdepth 1 -name '*.ver' | wc -l | tr -d ' ')"
  if [[ "$count" == "2" ]]; then
    ok "finalize: both non-preproduction .ver files copied to locked"
  else
    fail "finalize: expected 2 .ver files in locked, found $count"
  fi
  rm -rf "$d"
}

test_finalize_clears_old_locked_files() {
  require_proc_sub "finalize: stale locked files removed before re-copy" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries" "$d/version/locked"
  write_ver_ga "$d/version/locked/old_stale.ver" 0 9 9
  write_ver_ga "$d/version/entries/2_0_0_new.ver" 2 0 0

  REPO_ROOT="$d" bash "$FINALIZE" >/dev/null

  if [[ ! -f "$d/version/locked/old_stale.ver" ]]; then
    ok "finalize: stale locked file removed after re-copy"
  else
    fail "finalize: stale locked file still present after finalize"
  fi
  if [[ -f "$d/version/locked/2_0_0_new.ver" ]]; then
    ok "finalize: new entries file appears in locked"
  else
    fail "finalize: new entries file missing from locked"
  fi
  rm -rf "$d"
}

# ── Tests: promote_preproduction_for_main.sh ──────────────────────────────────

test_promote_basic_creates_root_ga_ver() {
  require_proc_sub "promote: GM=1 preproduction dir → root GA .ver in entries" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 3.0.0" "$d/version/locked"

  # Single GM=1 file in the preproduction dir
  cat >"$d/version/entries/preproduction 3.0.0/build_gm.ver" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=2
DESCRIPTION=GA release candidate
VER

  REPO_ROOT="$d" bash "$PROMOTE" >/dev/null 2>&1

  # A root GA .ver must now exist under version/entries/
  local ga_count
  ga_count="$(find "$d/version/entries" -maxdepth 1 -name '*.ver' | wc -l | tr -d ' ')"
  if [[ "$ga_count" -ge 1 ]]; then
    ok "promote: root GA .ver created under version/entries/"
  else
    fail "promote: no root GA .ver found after promotion (count=$ga_count)"
  fi
  # Promoted file must not contain PRERELEASE, GM, or DEV_VERSION keys
  local ga_file
  ga_file="$(find "$d/version/entries" -maxdepth 1 -name '*.ver' | head -1)"
  if [[ -n "$ga_file" ]]; then
    if grep -qE '^(PRERELEASE|GM|DEV_VERSION)=' "$ga_file" 2>/dev/null; then
      fail "promote: promoted GA .ver must not contain PRERELEASE/GM/DEV_VERSION keys"
    else
      ok "promote: promoted GA .ver has no PRERELEASE/GM/DEV_VERSION keys"
    fi
    # Must contain valid semver fields
    if grep -q 'MAJOR_VERSION=3' "$ga_file"; then
      ok "promote: promoted GA .ver has MAJOR_VERSION=3"
    else
      fail "promote: promoted GA .ver missing MAJOR_VERSION=3"
    fi
  fi
  rm -rf "$d"
}

test_promote_removes_preproduction_dir() {
  require_proc_sub "promote: preproduction dir removed after promotion" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 3.0.0" "$d/version/locked"

  cat >"$d/version/entries/preproduction 3.0.0/gm.ver" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=1
DESCRIPTION=Promote me
VER

  REPO_ROOT="$d" bash "$PROMOTE" >/dev/null 2>&1

  if [[ ! -d "$d/version/entries/preproduction 3.0.0" ]]; then
    ok "promote: preproduction 3.0.0/ dir removed after promotion"
  else
    fail "promote: preproduction 3.0.0/ dir still present after promotion"
  fi
  rm -rf "$d"
}

test_promote_skips_dir_without_gm() {
  require_proc_sub "promote: preproduction dir without GM=1 is not promoted" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 2.0.0" "$d/version/locked"
  write_ver_prerelease "$d/version/entries/preproduction 2.0.0/pre.ver" 2 0 0 1

  REPO_ROOT="$d" bash "$PROMOTE" >/dev/null 2>&1

  # No root .ver should have appeared; the preproduction dir should remain
  local root_count
  root_count="$(find "$d/version/entries" -maxdepth 1 -name '*.ver' | wc -l | tr -d ' ')"
  if [[ "$root_count" == "0" ]]; then
    ok "promote: no root GA .ver created when GM=1 is absent"
  else
    fail "promote: unexpected root GA .ver created without GM=1 (count=$root_count)"
  fi
  if [[ -d "$d/version/entries/preproduction 2.0.0" ]]; then
    ok "promote: preproduction dir preserved when no GM=1 file"
  else
    fail "promote: preproduction dir removed despite no GM=1 file"
  fi
  rm -rf "$d"
}

test_promote_multiple_ver_files_aggregated() {
  require_proc_sub "promote: multiple prerelease .ver aggregated into one root GA .ver" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 3.0.0" "$d/version/locked"

  write_ver_prerelease "$d/version/entries/preproduction 3.0.0/build1.ver" 3 0 0 1
  cat >"$d/version/entries/preproduction 3.0.0/build2_gm.ver" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=2
DESCRIPTION=Second build, GM
VER

  REPO_ROOT="$d" bash "$PROMOTE" >/dev/null 2>&1

  # One root GA .ver should now exist
  local root_count
  root_count="$(find "$d/version/entries" -maxdepth 1 -name '*.ver' | wc -l | tr -d ' ')"
  if [[ "$root_count" == "1" ]]; then
    ok "promote: exactly one root GA .ver created from two prerelease builds"
  else
    fail "promote: expected 1 root GA .ver, found $root_count"
  fi
  rm -rf "$d"
}

test_promote_rejects_multiple_gm_in_same_dir() {
  require_proc_sub "promote: two GM=1 files in same preproduction dir cause non-zero exit" || return 0
  local d
  d="$(mktemp -d)"
  mkdir -p "$d/version/entries/preproduction 3.0.0" "$d/version/locked"

  cat >"$d/version/entries/preproduction 3.0.0/gm1.ver" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=1
DESCRIPTION=First GM
VER
  cat >"$d/version/entries/preproduction 3.0.0/gm2.ver" <<VER
MAJOR_VERSION=3
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
GM=1
DEV_VERSION=2
DESCRIPTION=Second GM (not allowed)
VER

  if REPO_ROOT="$d" bash "$PROMOTE" 2>/dev/null; then
    fail "promote: two GM=1 files should cause failure"
  else
    ok "promote: two GM=1 in same preproduction dir causes non-zero exit"
  fi
  rm -rf "$d"
}

# ── Run new tests ─────────────────────────────────────────────────────────────

# gen_version_def.sh
test_gen_version_def_no_prerelease_version_line_matches
test_gen_version_def_prerelease_version_line_shown
test_gen_version_def_dev_version_in_version_line
test_gen_version_def_picks_highest_prerelease
test_gen_version_def_fails_no_locked_vers

# finalize_version_locked.sh
test_finalize_excludes_preproduction_dirs
test_finalize_copies_non_preproduction_files
test_finalize_clears_old_locked_files

# promote_preproduction_for_main.sh
test_promote_basic_creates_root_ga_ver
test_promote_removes_preproduction_dir
test_promote_skips_dir_without_gm
test_promote_multiple_ver_files_aggregated
test_promote_rejects_multiple_gm_in_same_dir

# ── Summary ───────────────────────────────────────────────────────────────────

echo ""


if (( FAIL > 0 )); then
  exit 1
fi
exit 0

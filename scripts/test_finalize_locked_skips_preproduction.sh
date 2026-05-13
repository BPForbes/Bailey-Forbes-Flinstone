#!/usr/bin/env bash
# Ensure finalize_version_locked.sh does not copy preproduction */ into version/locked.
set -euo pipefail
SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export REPO_ROOT="$TMP"
mkdir -p "$TMP/version/entries/preproduction 9.9.9" "$TMP/version/locked"
echo 'MAJOR_VERSION=9
STANDARD_VERSION=9
RELEASE_VERSION=9
DESCRIPTION=Root GA only.' >"$TMP/version/entries/9_9_9_root.ver"
echo 'MAJOR_VERSION=9
STANDARD_VERSION=9
RELEASE_VERSION=9
PRERELEASE=1
DEV_VERSION=1
DESCRIPTION=Should not appear in locked.' >"$TMP/version/entries/preproduction 9.9.9/hidden.ver"
printf '%s\n' 'stub ABOUT for temp tree' >"$TMP/version/entries/ABOUT.txt"
"$SCRIPTS/finalize_version_locked.sh"
[[ -f "$TMP/version/locked/9_9_9_root.ver" ]]
[[ ! -d "$TMP/version/locked/preproduction 9.9.9" ]]
[[ ! -f "$TMP/version/locked/preproduction 9.9.9/hidden.ver" ]]
echo "test_finalize_locked_skips_preproduction: ok"

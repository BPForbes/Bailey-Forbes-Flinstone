#!/usr/bin/env bash
# Self-test: promote_preproduction_for_main merges all preproduction .ver rows by DEV_VERSION,
# writes a root GA file without PRERELEASE/GM/DEV_VERSION, and deletes the preproduction directory.
set -euo pipefail
ROOT_SCRIPT="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export REPO_ROOT="$TMP"
mkdir -p "$TMP/version/entries/preproduction 4.0.0"
cat >"$TMP/version/entries/preproduction 4.0.0/4_0_0_alpha.ver" <<'EOF'
MAJOR_VERSION=4
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DEV_VERSION=1
GM=0
DESCRIPTION=First preproduction slice.
EOF
cat >"$TMP/version/entries/preproduction 4.0.0/4_0_0_finalize.ver" <<'EOF'
MAJOR_VERSION=4
STANDARD_VERSION=0
RELEASE_VERSION=0
PRERELEASE=1
DEV_VERSION=2
GM=1
DESCRIPTION=Second slice with go-to-main flag.
EOF
"$ROOT_SCRIPT/promote_preproduction_for_main.sh"
[[ ! -d "$TMP/version/entries/preproduction 4.0.0" ]]
[[ -f "$TMP/version/entries/4_0_0_finalize.ver" ]]
grep -q 'DEV_VERSION 1 (4_0_0_alpha.ver):' "$TMP/version/entries/4_0_0_finalize.ver"
grep -q 'DEV_VERSION 2 (4_0_0_finalize.ver):' "$TMP/version/entries/4_0_0_finalize.ver"
if grep -qE '^[[:space:]]*(int[[:space:]]+)?(PRERELEASE|GM|DEV_VERSION)=' "$TMP/version/entries/4_0_0_finalize.ver"; then
  echo "test_finalize_preproduction_gm: promoted file must omit PRERELEASE, GM, and DEV_VERSION keys" >&2
  exit 1
fi
echo "test_finalize_preproduction_gm: ok"

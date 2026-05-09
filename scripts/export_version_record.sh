#!/usr/bin/env bash
# Emit a unified version record from userland/shell/version_def.h and the changelog source.
# Usage: scripts/export_version_record.sh [--json]
# For multi-branch deployment: run once per checkout and concatenate or merge outputs.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEF="$ROOT/userland/shell/version_def.h"
LOGSRC="$ROOT/userland/shell/version_changelog.c"

die() { echo "$*" >&2; exit 1; }

[[ -f "$DEF" ]] || die "missing $DEF"

pick_int() {
    local name="$1"
    grep -E "^#define ${name}[[:space:]]+" "$DEF" | awk '{print $3}' | head -1
}

MAJOR="$(pick_int VERSION_MAJOR)"
STD="$(pick_int VERSION_STANDARD)"
PATCH="$(pick_int VERSION_PATCH)"
[[ -n "$MAJOR" && -n "$STD" && -n "$PATCH" ]] || die "could not parse version integers from $DEF"

VER="${MAJOR}.${STD}.${PATCH}"

CHANGELOG_NOTE=""
if [[ -f "$LOGSRC" ]]; then
    CHANGELOG_NOTE="$(sed -n '/const char VERSION_CHANGELOG/,/;/p' "$LOGSRC" | tr -d '\r' || true)"
fi

if [[ "${1:-}" == "--json" ]]; then
    printf '{"major":%s,"standard":%s,"patch":%s,"version":"%s","changelog_source":"userland/shell/version_changelog.c"}\n' \
        "$MAJOR" "$STD" "$PATCH" "$VER"
else
    echo "version=${VER}"
    echo "VERSION_MAJOR=${MAJOR}"
    echo "VERSION_STANDARD=${STD}"
    echo "VERSION_PATCH=${PATCH}"
    echo ""
    echo "--- VERSION_CHANGELOG (source excerpt) ---"
    echo "$CHANGELOG_NOTE"
fi

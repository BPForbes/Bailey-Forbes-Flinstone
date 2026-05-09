#!/usr/bin/env bash
# Emit a unified version record from userland/shell/version_def.h.
# Optional: if deployment generates userland/shell/version_changelog.c, excerpt it — not used by default `make`.
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
    if [[ -f "$LOGSRC" ]]; then
        printf '{"major":%s,"standard":%s,"patch":%s,"version":"%s","changelog_source":"userland/shell/version_changelog.c"}\n' \
            "$MAJOR" "$STD" "$PATCH" "$VER"
    else
        printf '{"major":%s,"standard":%s,"patch":%s,"version":"%s","changelog_source":null}\n' \
            "$MAJOR" "$STD" "$PATCH" "$VER"
    fi
else
    echo "version=${VER}"
    echo "VERSION_MAJOR=${MAJOR}"
    echo "VERSION_STANDARD=${STD}"
    echo "VERSION_PATCH=${PATCH}"
    echo ""
    if [[ -f "$LOGSRC" ]]; then
        echo "--- VERSION_CHANGELOG (optional deployment file present) ---"
        echo "$CHANGELOG_NOTE"
    else
        echo "--- VERSION_CHANGELOG ---"
        echo "(none — local/dev build does not compile changelog; add at deployment if needed)"
    fi
fi

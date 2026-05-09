#!/usr/bin/env bash
# Fail if version/locked is not a byte-for-byte mirror of version/entries.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"
LCK="$ROOT/version/locked"
mkdir -p "$ENT" "$LCK"
if ! diff -rq "$ENT" "$LCK" >/dev/null; then
  echo "version/locked must mirror version/entries exactly." >&2
  echo "Run: ./scripts/sync_version_locked_mirror.sh" >&2
  diff -rq "$ENT" "$LCK" >&2 || true
  exit 1
fi

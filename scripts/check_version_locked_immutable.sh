#!/usr/bin/env bash
# Reject PRs that modify or delete existing version/locked/* files present on the merge base.
# Published release record lives under version/locked; version/entries is WIP until finalized.
#
# Usage: scripts/check_version_locked_immutable.sh <base-commit-ish>
set -euo pipefail
BASE="${1:?usage: $0 <base-commit-ish>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

rc=0
while IFS= read -r file; do
  if git cat-file -e "${BASE}:${file}" 2>/dev/null; then
    if ! git diff --quiet "${BASE}" -- "$file"; then
      echo "error: cannot modify or remove finalized version file (immutable): $file" >&2
      echo "Add or edit work under version/entries/, then run finalize_version_locked.sh." >&2
      rc=1
    fi
  fi
done < <(git ls-tree -r --name-only "${BASE}" -- version/locked)

exit "$rc"

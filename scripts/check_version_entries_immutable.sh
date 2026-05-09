#!/usr/bin/env bash
# Reject PRs that modify existing version/entries/*.ver files present on the merge base.
# New files under version/entries/ are allowed.
# Usage: scripts/check_version_entries_immutable.sh <base-commit>
set -euo pipefail
BASE="${1:?usage: $0 <base-commit-ish>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

rc=0
while IFS= read -r file; do
  case "$file" in
    *.ver) ;;
    *) continue ;;
  esac
  if git cat-file -e "${BASE}:${file}" 2>/dev/null; then
    if ! git diff --quiet "${BASE}" -- "$file"; then
      echo "error: cannot modify merged version entry (immutable): $file" >&2
      echo "Add a new file under version/entries/ instead." >&2
      rc=1
    fi
  fi
done < <(git ls-files version/entries)

exit "$rc"

#!/usr/bin/env bash
# Reject PRs that modify or delete existing version/entries/*.ver files present on the merge base.
# New files under version/entries/ are allowed.
#
# Immutability is enforced relative to the merge base (what already shipped on the target branch).
# Entry files that exist only on the PR branch are not "locked" by this script until they land on
# the target via merge—revise or remove them on the feature branch as needed before merge.
#
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
      echo "error: cannot modify or remove merged version entry (immutable): $file" >&2
      echo "Add a new file under version/entries/ instead." >&2
      rc=1
    fi
  fi
done < <(git ls-tree -r --name-only "${BASE}" -- version/entries)

exit "$rc"

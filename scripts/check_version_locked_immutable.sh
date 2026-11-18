#!/usr/bin/env bash
# Reject PRs that modify or delete existing version/locked/* files present on the merge base.
# Published release record lives under version/locked; version/entries is updated until finalize copies it forward.
# version/locked/ABOUT.txt is excluded: it is instructional prose mirrored from version/entries/ABOUT.txt,
# not a semver .ver release file, and may legitimately change in the same PR as entries.
# A fold (scripts/fold_rerelease_entries.sh) is the one change allowed to a published row: its
# DESCRIPTION may only grow by an append (every other line unchanged), and a published file may only
# be removed when the base held it in version/entries as a RERELEASE=1 row.
#
# Usage: scripts/check_version_locked_immutable.sh <base-commit-ish>
set -euo pipefail
BASE="${1:?usage: $0 <base-commit-ish>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# One-time repair exemption: the base copy of this record carries unresolved
# merge-conflict markers (from #356), and develop holds the clean copy. Remove
# this entry once the promotion that carries the repair has merged.
REPAIRED_ON_MAIN=(
  "version/locked/4_5_1_relay_ci_cloudflare_lab.ver"
)

# 0 when the change to a published file is exactly what a fold makes.
is_fold() {
  local file="$1" rel="${1#version/locked/}"
  if [[ ! -f "$file" ]]; then
    git show "${BASE}:version/entries/${rel}" 2>/dev/null |
      grep -qE '^[[:space:]]*(int[[:space:]]+)?RERELEASE=1[[:space:]]*$'
    return
  fi
  python3 -I - "$file" <(git show "${BASE}:${file}") <<'PY'
import re, sys
new = open(sys.argv[1], encoding="utf-8").read().splitlines()
old = open(sys.argv[2], encoding="utf-8").read().splitlines()

def span(lines):
    for i, line in enumerate(lines):
        m = re.match(r"^\s*(?:int\s+)?DESCRIPTION(=|<<)", line)
        if not m:
            continue
        if m.group(1) == "=":
            return i, i
        delim = line.split("<<", 1)[1].strip()
        for j in range(i + 1, len(lines)):
            if lines[j].strip() == delim:
                return i, j
    return None

a, b = span(old), span(new)
if not a or not b or old[:a[0]] != new[:b[0]] or old[a[1] + 1:] != new[b[1] + 1:]:
    sys.exit(1)
if a[0] == a[1]:  # one line: the new value is the old one plus " " and more
    ok = b[0] == b[1] and new[b[0]].startswith(old[a[0]].rstrip() + " ")
else:  # a block: the old lines come first, the same delimiters close it
    ok = b[0] != b[1] and new[b[0]] == old[a[0]] and new[b[1]] == old[a[1]] and new[b[0]:b[0] + (a[1] - a[0])] == old[a[0]:a[1]] and b[1] > a[1] - a[0] + b[0]
sys.exit(0 if ok else 1)
PY
}

rc=0
while IFS= read -r file; do
  [[ "$file" == "version/locked/ABOUT.txt" ]] && continue
  skip=0
  for repaired in "${REPAIRED_ON_MAIN[@]}"; do
    [[ "$file" == "$repaired" ]] && skip=1
  done
  [[ "$skip" -eq 1 ]] && continue
  if git cat-file -e "${BASE}:${file}" 2>/dev/null; then
    if ! git diff --quiet "${BASE}" -- "$file" && ! is_fold "$file"; then
      echo "error: cannot modify or remove finalized version file (immutable): $file" >&2
      echo "Add or edit work under version/entries/, then run finalize_version_locked.sh." >&2
      rc=1
    fi
  fi
done < <(git ls-tree -r --name-only "${BASE}" -- version/locked)

exit "$rc"

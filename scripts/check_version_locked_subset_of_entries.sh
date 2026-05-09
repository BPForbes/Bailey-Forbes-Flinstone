#!/usr/bin/env bash
# Every file under version/locked must exist under version/entries with identical content.
# version/entries may contain extra files not yet copied to version/locked.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"
LCK="$ROOT/version/locked"
if [[ ! -d "$ENT" ]]; then
  echo "error: missing $ENT" >&2
  exit 1
fi
if [[ ! -d "$LCK" ]]; then
  echo "error: missing $LCK" >&2
  echo "Run: ./scripts/finalize_version_locked.sh" >&2
  exit 1
fi
if ! find "$LCK" -type f | head -n1 | grep -q .; then
  echo "error: version/locked has no files" >&2
  exit 1
fi

rc=0
while IFS= read -r -d '' lf; do
  rel="${lf#"$LCK"/}"
  ef="$ENT/$rel"
  if [[ ! -f "$ef" ]]; then
    echo "error: locked file has no counterpart in entries: $rel" >&2
    rc=1
    continue
  fi
  if ! cmp -s "$lf" "$ef"; then
    echo "error: locked and entries differ for $rel (run finalize_version_locked.sh to copy entries → locked, or align entries)" >&2
    rc=1
  fi
done < <(find "$LCK" -type f -print0)

exit "$rc"

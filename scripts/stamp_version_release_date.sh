#!/usr/bin/env bash
# Append RELEASE_DATE=YYYY-MM-DD to each *.ver under version/locked and
# version/entries when that key is absent. Intended to run after
# finalize_version_locked.sh (entries and locked are mirrors) so merge-time
# dates are recorded before gen_version_def.sh and changelog generation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LCK="$ROOT/version/locked"
ENT="$ROOT/version/entries"
d="$(date +%Y-%m-%d)"

# True when a top-level RELEASE_DATE= assignment exists outside DESCRIPTION<<… heredocs
# (grep alone can match example lines inside heredoc bodies).
release_date_key_present() {
  local f=$1
  awk '
    BEGIN { in_h = 0; found = 0 }
    function trim(s,   t) {
      t = s
      gsub(/^[[:space:]]+/, "", t)
      gsub(/[[:space:]]+$/, "", t)
      return t
    }
    {
      line = $0
      sub(/\r$/, "", line)
      t = trim(line)
      if (length(t) == 0 || substr(t, 1, 1) == "#") next
      sk = t
      if (sk ~ /^int[[:space:]]+/) {
        sub(/^int[[:space:]]+/, "", sk)
        sk = trim(sk)
      }
      if (in_h) {
        if (t == delim) in_h = 0
        next
      }
      if (index(sk, "DESCRIPTION<<") == 1) {
        rest = substr(sk, 14)
        delim = trim(rest)
        if (length(delim) > 0) in_h = 1
        next
      }
      if (sk ~ /^RELEASE_DATE=/) {
        found = 1
        exit
      }
    }
    END { exit(found ? 0 : 1) }
  ' "$f"
}

stamp_if_missing() {
  local f=$1
  [[ -f "$f" ]] || return 0
  if release_date_key_present "$f"; then
    return 0
  fi
  printf '\nRELEASE_DATE=%s\n' "$d" >>"$f"
}

shopt -s nullglob
for f in "$LCK"/*.ver; do
  stamp_if_missing "$f"
  bf="$ENT/$(basename "$f")"
  stamp_if_missing "$bf"
done

echo "stamp_version_release_date: stamped missing RELEASE_DATE (as $d) under $LCK and $ENT"

#!/usr/bin/env bash
# Validate prerelease .ver layout under version/entries:
#   PRERELEASE=1  →  file must live in version/entries/preproduction <A>.<B>.<C>/
#   Top-level .ver  →  PRERELEASE must be 0 or absent (treated as 0).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENT="$ROOT/version/entries"
err=0

get_field() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/' || true
}

while IFS= read -r -d '' f; do
  rel="${f#"$ENT"/}"
  parent=$(dirname "$rel")
  pr=$(get_field PRERELEASE "$f")
  [[ -n "$pr" ]] || pr=0
  if ! [[ "$pr" =~ ^[0-9]+$ ]]; then
    echo "check_version_prerelease_layout: invalid PRERELEASE in $f" >&2
    err=1
    continue
  fi
  m=$(get_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(get_field VERSION_MAJOR "$f")
  s=$(get_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(get_field VERSION_STANDARD "$f")
  r=$(get_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(get_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "check_version_prerelease_layout: missing semver in $f" >&2
    err=1
    continue
  fi
  exp_dir="preproduction ${m}.${s}.${r}"
  if [[ "$parent" == "." ]]; then
    if [[ "$pr" -eq 1 ]]; then
      echo "check_version_prerelease_layout: PRERELEASE=1 must live under ${ENT}/${exp_dir}/ — $f" >&2
      err=1
    fi
  else
    if [[ "$parent" != "$exp_dir" ]]; then
      echo "check_version_prerelease_layout: directory '$parent' must be exactly '${exp_dir}' for semver ${m}.${s}.${r} — $f" >&2
      err=1
    fi
    if [[ "$pr" -ne 1 ]]; then
      echo "check_version_prerelease_layout: under ${parent}/ PRERELEASE must be 1 — $f" >&2
      err=1
    fi
  fi
done < <(find "$ENT" -type f -name '*.ver' -print0)

if (( err )); then
  exit 1
fi
echo "check_version_prerelease_layout: ok"

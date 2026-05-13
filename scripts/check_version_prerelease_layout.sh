#!/usr/bin/env bash
# Validate .ver layout under version/entries:
#   Root .ver: GM must be 0 or absent (never GM=1 at root). PRERELEASE may be 0/absent
#   (GA-oriented) or 1 (prerelease rows authored at the root for CI relocate — same-repo
#   workflow: do not hand-add new PRERELEASE=1 *.ver under preproduction */; fork PRs run
#   relocate locally, then commit the resulting tree per docs).
#   Root PRERELEASE=1: require DEV_VERSION>=1 (same iteration rules as under preproduction).
#   Under preproduction <A>.<B>.<C>/: every .ver must have PRERELEASE=1, DEV_VERSION>=1,
#   and at most one file in that directory may set GM=1.
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
  gm=$(get_field GM "$f")
  [[ -n "$gm" ]] || gm=0
  dv=$(get_field DEV_VERSION "$f")
  if ! [[ "$pr" =~ ^[0-9]+$ ]]; then
    echo "check_version_prerelease_layout: invalid PRERELEASE in $f" >&2
    err=1
    continue
  fi
  if ! [[ "$gm" =~ ^[0-9]+$ ]]; then
    echo "check_version_prerelease_layout: invalid GM in $f" >&2
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
      if [[ -z "$dv" ]] || ! [[ "$dv" =~ ^[0-9]+$ ]] || (( dv < 1 )); then
        echo "check_version_prerelease_layout: root PRERELEASE=1 requires DEV_VERSION>=1 — $f" >&2
        err=1
      fi
    fi
    if [[ "$gm" -eq 1 ]]; then
      echo "check_version_prerelease_layout: GM=1 is not allowed on root .ver files — $f" >&2
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
    if [[ -z "$dv" ]] || ! [[ "$dv" =~ ^[0-9]+$ ]] || (( dv < 1 )); then
      echo "check_version_prerelease_layout: under ${parent}/ each .ver needs DEV_VERSION>=1 — $f" >&2
      err=1
    fi
  fi
done < <(find "$ENT" -type f -name '*.ver' -print0)

while IFS= read -r -d '' dir; do
  gm_n=0
  while IFS= read -r -d '' vf; do
    [[ -f "$vf" ]] || continue
    if grep -qE '^[[:space:]]*(int[[:space:]]+)?GM=1([[:space:]]|$)' "$vf"; then
      gm_n=$((gm_n + 1))
    fi
  done < <(find "$dir" -maxdepth 1 -type f -name '*.ver' -print0 2>/dev/null)
  if (( gm_n > 1 )); then
    echo "check_version_prerelease_layout: at most one GM=1 allowed in $dir (found $gm_n)" >&2
    err=1
  fi
done < <(find "$ENT" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0 2>/dev/null)

if (( err )); then
  exit 1
fi
echo "check_version_prerelease_layout: ok"

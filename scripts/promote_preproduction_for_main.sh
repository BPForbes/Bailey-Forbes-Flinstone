#!/usr/bin/env bash
# Finalize preproduction A.B.C/ when one .ver in that directory sets GM=1 (go-to-main).
#
# For each version/entries and version/locked tree:
#   (finalize_version_locked.sh does not copy preproduction */ into locked, so
#    version/locked usually has nothing to merge here; the locked pass still removes
#    legacy preproduction */ trees if they exist.)
#   - For each top-level directory named "preproduction <A>.<B>.<C>/":
#     - If no *.ver contains GM=1, skip (still in active prerelease).
#     - Require exactly one GM=1 among *.ver (else error).
#     - Require every *.ver in the directory to have PRERELEASE=1 and DEV_VERSION>=1.
#     - Sort all *.ver by DEV_VERSION ascending; concatenate each DESCRIPTION into one
#       itemized block (DEV_VERSION order), write a new GA .ver at the tree root using
#       the basename of the GM=1 file, with no PRERELEASE, GM, or DEV_VERSION lines.
#     - Remove the entire preproduction directory from that tree.
#
# Run before merging version trees into main. With REPO_ROOT set, operates on that tree
# (used by scripts/test_finalize_preproduction_gm.sh).
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

extract_description() {
  python3 - "$1" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
m = re.search(r"^(?:[ \t]*(?:int[ \t]+)?)?DESCRIPTION<<([^\s]+)\s*$", text, re.M)
if m:
    tag = m.group(1)
    rest = text[m.end() :]
    end = re.search(r"^%s\s*$" % re.escape(tag), rest, re.M)
    if not end:
        sys.exit(2)
    print(rest[: end.start()].strip())
    sys.exit(0)
m2 = re.search(r"^(?:[ \t]*(?:int[ \t]+)?)?DESCRIPTION=(.*)$", text, re.M)
if m2:
    v = m2.group(1).strip().strip('"')
    print(v)
    sys.exit(0)
sys.exit(1)
PY
}

get_field() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/' || true
}

semver_from_dirname() {
  # "preproduction 4.0.0" -> 4 0 0
  local d="$1"
  local rest="${d#preproduction }"
  local IFS=.
  read -r ma st rel <<<"$rest"
  [[ -n "$ma" && -n "$st" && -n "$rel" ]] || return 1
  echo "$ma $st $rel"
}

promote_root() {
  local BASE="$1"
  [[ -d "$BASE" ]] || return 0
  while IFS= read -r -d '' dir; do
    local gm_file="" gm_count=0 vf
    shopt -s nullglob
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      if grep -qE '^[[:space:]]*(int[[:space:]]+)?GM=1([[:space:]]|$)' "$vf"; then
        gm_count=$((gm_count + 1))
        gm_file=$vf
      fi
    done
    if (( gm_count == 0 )); then
      continue
    fi
    if (( gm_count != 1 )); then
      echo "promote_preproduction_for_main: expected exactly one GM=1 in $dir, found $gm_count" >&2
      exit 1
    fi

    local dname ma st rel semver
    dname=$(basename "$dir")
    if ! semver=$(semver_from_dirname "$dname"); then
      echo "promote_preproduction_for_main: bad directory name (want 'preproduction A.B.C'): $dir" >&2
      exit 1
    fi
    read -r ma st rel <<<"$semver"

    local files_sorted="" lines="" desc dv bn f
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      if ! grep -qE '^[[:space:]]*(int[[:space:]]+)?PRERELEASE=1([[:space:]]|$)' "$vf"; then
        echo "promote_preproduction_for_main: $vf must have PRERELEASE=1 under $dir" >&2
        exit 1
      fi
      dv=$(get_field DEV_VERSION "$vf")
      [[ -n "$dv" ]] && [[ "$dv" =~ ^[0-9]+$ ]] && (( dv >= 1 )) || {
        echo "promote_preproduction_for_main: $vf needs DEV_VERSION>=1 under $dir" >&2
        exit 1
      }
      local line
      printf -v line '%05d\t%s\n' "$dv" "$vf"
      files_sorted+="$line"
    done
    files_sorted=$(printf '%s' "$files_sorted" | sort -n)

    local dest="$BASE/$(basename "$gm_file")"
    if [[ -e "$dest" ]]; then
      echo "promote_preproduction_for_main: refuse to overwrite existing $dest" >&2
      exit 1
    fi

    while IFS=$'\t' read -r _ f; do
      [[ -n "$f" ]] || continue
      [[ -f "$f" ]] || continue
      dv=$(get_field DEV_VERSION "$f")
      bn=$(basename "$f")
      desc=$(extract_description "$f") || {
        echo "promote_preproduction_for_main: could not read DESCRIPTION in $f" >&2
        exit 1
      }
      desc=${desc//$'\r'/}
      lines+="DEV_VERSION ${dv} (${bn}): ${desc}"$'\n\n'
    done < <(printf '%s\n' "$files_sorted")

    {
      echo "MAJOR_VERSION=${ma}"
      echo "STANDARD_VERSION=${st}"
      echo "RELEASE_VERSION=${rel}"
      if grep -qE '^[[:space:]]*(int[[:space:]]+)?RELEASE_DATE=' "$gm_file"; then
        grep -E '^[[:space:]]*(int[[:space:]]+)?RELEASE_DATE=' "$gm_file" | head -1
      fi
      echo "DESCRIPTION<<PROMOTED_GA_DESCRIPTION"
      printf '%s' "$lines"
      echo "PROMOTED_GA_DESCRIPTION"
    } >"$dest"

    rm -rf "$dir"
    echo "promote_preproduction_for_main: merged $dir → $(basename "$dest") (no PRERELEASE/GM/DEV_VERSION) under $BASE"
  done < <(find "$BASE" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0)
}

promote_root "$ROOT/version/entries"
promote_root "$ROOT/version/locked"
echo "promote_preproduction_for_main: done"

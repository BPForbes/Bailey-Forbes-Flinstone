#!/usr/bin/env bash
# Promote preproduction A.B.C/ directories that contain exactly one GM=1 .ver:
#   - Sort every *.ver in that folder by DEV_VERSION ascending.
#   - Build one new root GA .ver (basename from the GM=1 file) whose DESCRIPTION itemizes
#     each source file's description (DEV_VERSION order), with no PRERELEASE, GM, or DEV_VERSION keys.
#   - Remove the entire preproduction directory (and from locked only if it existed there).
#
# finalize_version_locked.sh does not copy preproduction */ into locked; locked may lack
# the folder until a prior manual copy — this script still cleans locked if present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

get_field() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[^=]*=[[:space:]]*([0-9]+).*/\1/' || true
}

extract_description() {
  python3 - "$1" <<'PY'
import re, sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8", errors="replace").read().splitlines()
desc = ""
i = 0
while i < len(text):
    raw = text[i].rstrip("\r")
    t = raw.strip()
    if not t or t.startswith("#"):
        i += 1
        continue
    sk = t
    if sk.startswith("int "):
        sk = sk[4:].strip()
    if sk.startswith("DESCRIPTION<<"):
        delim = sk[14:].strip()
        i += 1
        buf = []
        while i < len(text):
            tl = text[i].strip("\r").strip()
            if tl == delim:
                i += 1
                break
            buf.append(text[i].rstrip("\r"))
            i += 1
        desc = "\n".join(buf).strip()
        break
    if sk.startswith("DESCRIPTION="):
        desc = sk.split("=", 1)[1].strip()
        if len(desc) >= 2 and desc[0] == '"' and desc[-1] == '"':
            desc = desc[1:-1]
        break
    i += 1
sys.stdout.write(desc)
PY
}

promote_root() {
  local BASE="$1"
  [[ -d "$BASE" ]] || return 0
  while IFS= read -r -d '' dir; do
    local gm_file="" gm_n=0 vf
    shopt -s nullglob
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      if grep -qE '^[[:space:]]*(int[[:space:]]+)?GM=1([[:space:]]|$)' "$vf"; then
        gm_n=$((gm_n + 1))
        gm_file=$vf
      fi
    done
    shopt -u nullglob
    (( gm_n == 0 )) && continue
    if (( gm_n > 1 )); then
      echo "promote_preproduction_for_main: at most one GM=1 allowed in $dir (found $gm_n)" >&2
      exit 1
    fi
    [[ -n "$gm_file" ]] || continue

    local sort_tmp
    sort_tmp=$(mktemp)
    shopt -s nullglob
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      dv=$(get_field DEV_VERSION "$vf")
      [[ -n "$dv" ]] || dv=0
      printf '%s\t%s\n' "$dv" "$vf"
    done | sort -t $'\t' -k1,1n >"$sort_tmp"
    shopt -u nullglob

    local agg delim="PROMOTE_DESC_END"
    agg=""
    while IFS=$'\t' read -r dv fpath; do
      [[ -f "$fpath" ]] || continue
      local one
      one=$(extract_description "$fpath")
      one=${one//$'\r'/}
      if [[ -n "$agg" ]]; then
        agg+=$'\n'
      fi
      agg+="- (DEV_VERSION ${dv}) ${one}"
    done <"$sort_tmp"
    rm -f "$sort_tmp"

    local dest="$BASE/$(basename "$gm_file")"
    if [[ -e "$dest" ]]; then
      echo "promote_preproduction_for_main: refuse to overwrite existing $dest" >&2
      exit 1
    fi

    m=$(get_field MAJOR_VERSION "$gm_file")
    [[ -n "$m" ]] || m=$(get_field VERSION_MAJOR "$gm_file")
    s=$(get_field STANDARD_VERSION "$gm_file")
    [[ -n "$s" ]] || s=$(get_field VERSION_STANDARD "$gm_file")
    r=$(get_field RELEASE_VERSION "$gm_file")
    [[ -n "$r" ]] || r=$(get_field MINOR_VERSION "$gm_file")
    [[ -n "$r" ]] || r=$(get_field VERSION_PATCH "$gm_file")
    if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
      echo "promote_preproduction_for_main: missing semver in $gm_file" >&2
      exit 1
    fi

    {
      printf 'MAJOR_VERSION=%s\n' "$m"
      printf 'STANDARD_VERSION=%s\n' "$s"
      printf 'RELEASE_VERSION=%s\n' "$r"
      if grep -qE '^[[:space:]]*(int[[:space:]]+)?RELEASE_DATE=' "$gm_file"; then
        grep -E '^[[:space:]]*(int[[:space:]]+)?RELEASE_DATE=' "$gm_file" | head -1
      fi
      printf 'DESCRIPTION<<%s\n' "$delim"
      printf '%s\n' "$agg"
      printf '%s\n' "$delim"
    } >"$dest"

    rm -f "$dir"/*.ver
    if ! rmdir "$dir" 2>/dev/null; then
      echo "promote_preproduction_for_main: could not remove $dir (non-.ver files present?)" >&2
      exit 1
    fi
    echo "promote_preproduction_for_main: promoted $(basename "$gm_file") from $dir → $(basename "$dest") (GA, aggregated DESCRIPTION)"
  done < <(find "$BASE" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0 2>/dev/null)
}

promote_root "$ROOT/version/entries"
promote_root "$ROOT/version/locked"
echo "promote_preproduction_for_main: done"

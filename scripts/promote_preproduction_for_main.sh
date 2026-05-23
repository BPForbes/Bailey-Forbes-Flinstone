#!/usr/bin/env bash
# Promote preproduction A.B.C/ directories that contain GM=1 .ver rows:
#   - If several GM=1 rows exist, select the one with the highest DEV_VERSION
#     and rewrite lower DEV_VERSION GM rows to GM=0.
#   - Build one new root GA .ver (basename from the GM=1 file) whose DESCRIPTION is
#     copied only from that GM=1 file (not a roll-up of other rows), with no PRERELEASE,
#     GM, or DEV_VERSION keys.
#   - Remove every *.ver in the preproduction directory, then remove the directory.
#
# finalize_version_locked.sh does not copy preproduction */ into locked; locked may lack
# the folder until a prior manual copy — this script still cleans locked if present.
set -euo pipefail
DRY_RUN=0
NORMALIZE_GM_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --dry-run)
      DRY_RUN=1
      ;;
    --normalize-gm-only)
      NORMALIZE_GM_ONLY=1
      ;;
    --help|-h)
      cat <<'HELP'
Usage: ./scripts/promote_preproduction_for_main.sh [--dry-run] [--normalize-gm-only]

Promote GM=1 rows from version/entries/preproduction A.B.C/ to root GA .ver files.

Options:
  --dry-run            Preview promotion and GM normalization without edits.
  --normalize-gm-only  Only demote duplicate lower DEV_VERSION GM=1 rows to GM=0.
  --help, -h           Show this help.

Examples:
  ./scripts/promote_preproduction_for_main.sh --dry-run
  ./scripts/promote_preproduction_for_main.sh
  ./scripts/promote_preproduction_for_main.sh --normalize-gm-only
HELP
      exit 0
      ;;
    *)
      echo "promote_preproduction_for_main: unknown argument: $arg" >&2
      exit 1
      ;;
  esac
done
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="promote_preproduction_for_main"

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

write_gm_zero() {
  python3 - "$1" <<'PY'
import os, re, sys, tempfile

path = sys.argv[1]
with open(path, "r", encoding="utf-8", errors="replace") as fh:
    lines = fh.readlines()

pat = re.compile(r"^(\s*(?:int\s+)?GM\s*=\s*)1(\s*)$")
changed = False
out = []
for line in lines:
    body = line[:-1] if line.endswith("\n") else line
    newline = "\n" if line.endswith("\n") else ""
    match = pat.match(body.rstrip("\r"))
    if match:
        out.append(f"{match.group(1)}0{match.group(2)}{newline}")
        changed = True
    else:
        out.append(line)

if changed:
    dirname = os.path.dirname(path) or "."
    fd, tmp = tempfile.mkstemp(prefix=".gm-zero-", dir=dirname, text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.writelines(out)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
PY
}

demote_lower_gm_files() {
  local selected="$1"
  shift
  local lower
  for lower in "$@"; do
    if (( DRY_RUN )); then
      echo "promote_preproduction_for_main: [dry-run] would set GM=0 in lower DEV_VERSION candidate $lower"
    else
      write_gm_zero "$lower"
      echo "promote_preproduction_for_main: set GM=0 in lower DEV_VERSION candidate $(basename "$lower") (selected $(basename "$selected"))"
    fi
  done
}

promote_root() {
  local BASE="$1"
  [[ -d "$BASE" ]] || return 0
  while IFS= read -r -d '' dir; do
    local gm_file="" gm_n=0 gm_dv=-1 vf gm_val dv
    local -a lower_gm_files=()
    shopt -s nullglob
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      gm_val=$(ver_parse_flag_field GM "$vf")
      if [[ "$gm_val" == "1" ]]; then
        gm_n=$((gm_n + 1))
        dv=$(ver_parse_nonneg_int_field DEV_VERSION "$vf")
        [[ -n "$dv" ]] || dv=0
        if (( dv > gm_dv )); then
          if [[ -n "$gm_file" ]]; then
            lower_gm_files+=("$gm_file")
          fi
          gm_file=$vf
          gm_dv=$dv
        elif (( dv == gm_dv )); then
          echo "promote_preproduction_for_main: multiple GM=1 files in $dir share DEV_VERSION=$dv; cannot choose a highest DEV_VERSION" >&2
          exit 1
        else
          lower_gm_files+=("$vf")
        fi
      fi
    done
    shopt -u nullglob
    (( gm_n == 0 )) && continue
    [[ -n "$gm_file" ]] || continue
    if (( gm_n > 1 )); then
      echo "promote_preproduction_for_main: selected $(basename "$gm_file") with DEV_VERSION=$gm_dv from $gm_n GM=1 candidates in $dir"
    fi

    if (( NORMALIZE_GM_ONLY )); then
      demote_lower_gm_files "$gm_file" "${lower_gm_files[@]}"
      continue
    fi

    local agg delim="PROMOTE_DESC_END"
    agg=$(extract_description "$gm_file")
    agg=${agg//$'\r'/}

    local dest="$BASE/$(basename "$gm_file")"
    if [[ -e "$dest" ]]; then
      echo "promote_preproduction_for_main: refuse to overwrite existing $dest" >&2
      exit 1
    fi

    m=$(ver_parse_nonneg_int_field MAJOR_VERSION "$gm_file")
    [[ -n "$m" ]] || m=$(ver_parse_nonneg_int_field VERSION_MAJOR "$gm_file")
    s=$(ver_parse_nonneg_int_field STANDARD_VERSION "$gm_file")
    [[ -n "$s" ]] || s=$(ver_parse_nonneg_int_field VERSION_STANDARD "$gm_file")
    r=$(ver_parse_nonneg_int_field RELEASE_VERSION "$gm_file")
    [[ -n "$r" ]] || r=$(ver_parse_nonneg_int_field MINOR_VERSION "$gm_file")
    [[ -n "$r" ]] || r=$(ver_parse_nonneg_int_field VERSION_PATCH "$gm_file")
    if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
      echo "promote_preproduction_for_main: missing semver in $gm_file" >&2
      exit 1
    fi

    if (( DRY_RUN )); then
      demote_lower_gm_files "$gm_file" "${lower_gm_files[@]}"
      echo "promote_preproduction_for_main: [dry-run] would write $dest from $gm_file (GA, DESCRIPTION from GM=1 file only)"
      if [[ "$BASE" == "$ROOT/version/entries" ]]; then
        echo "promote_preproduction_for_main: [dry-run] would clone to version/locked/$(basename "$gm_file")"
      fi
      echo "promote_preproduction_for_main: [dry-run] would remove $dir/*.ver and delete $dir"
      continue
    fi

    demote_lower_gm_files "$gm_file" "${lower_gm_files[@]}"

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

    # When promoting from entries, clone the GA file into version/locked/ immediately.
    if [[ "$BASE" == "$ROOT/version/entries" ]]; then
      local lck_dir="$ROOT/version/locked"
      mkdir -p "$lck_dir"
      cp "$dest" "$lck_dir/$(basename "$gm_file")"
      echo "promote_preproduction_for_main: cloned to locked: version/locked/$(basename "$gm_file")"
    fi

    rm -f "$dir"/*.ver
    if ! rmdir "$dir" 2>/dev/null; then
      echo "promote_preproduction_for_main: could not remove $dir (non-.ver files present?)" >&2
      exit 1
    fi
    echo "promote_preproduction_for_main: promoted $(basename "$gm_file") from $dir → $(basename "$dest") (GA, DESCRIPTION from GM=1 file only)"
  done < <(find "$BASE" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0 2>/dev/null)
}

promote_root "$ROOT/version/entries"
promote_root "$ROOT/version/locked"
if (( DRY_RUN )); then
  echo "promote_preproduction_for_main: dry-run complete (no files changed)"
elif (( NORMALIZE_GM_ONLY )); then
  echo "promote_preproduction_for_main: normalize complete"
else
  echo "promote_preproduction_for_main: done"
fi

#!/usr/bin/env bash
# Move version/entries/*.ver with PRERELEASE=1 from the entries root into
#   version/entries/preproduction <A>.<B>.<C>/
# using MAJOR/STANDARD/RELEASE (or alias keys) inside each file.
# Root rows already marked GM=1 are not relocated down into preproduction */;
# run promote_preproduction_for_main.sh to turn those rows into GA release notes.
#
# For each such file, if RELEASE_DATE= is not yet set, appends RELEASE_DATE=YYYY-MM-DD
# (calendar day of this run) while the file is still at the root — before the move.
# GitHub Actions then runs stamp_version_entries_release_date.sh on the entries root
# and every preproduction <A>.<B>.<C>/ tree so relocated rows still get a date on the
# same job. On develop, merge-time stamp_version_release_date.sh also walks
# version/locked and all version/entries/**/*.ver after finalize.
#
# Usage:
#   ./scripts/relocate_root_prerelease_ver_to_preproduction.sh
#   ./scripts/relocate_root_prerelease_ver_to_preproduction.sh --dry-run-exit-if-needed
#     (exit 1 if any root PRERELEASE=1 row exists; no edits — fork PR CI gate)
#
# REPO_ROOT may point at a non-default repo root (tests).
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
ENT="$ROOT/version/entries"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_release_date_stamp.sh
source "$SCRIPT_DIR/lib/ver_release_date_stamp.sh"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="relocate_root_prerelease_ver_to_preproduction"

dry_gate=0
if [[ "${1:-}" == "--dry-run-exit-if-needed" ]]; then
  dry_gate=1
fi

shopt -s nullglob
ver_root=( "$ENT"/*.ver )
shopt -u nullglob

need_reloc=0
for f in "${ver_root[@]}"; do
  [[ -f "$f" ]] || continue
  rel="${f#"$ENT"/}"
  [[ "$(dirname "$rel")" == "." ]] || continue
  pr=$(ver_parse_flag_field PRERELEASE "$f")
  gm=$(ver_parse_flag_field GM "$f")
  if [[ "$pr" -eq 1 && "$gm" -ne 1 ]]; then
    need_reloc=1
    break
  fi
done

if (( dry_gate )); then
  if (( need_reloc )); then
    echo "relocate_root_prerelease_ver_to_preproduction: root PRERELEASE=1 .ver files must be relocated into preproduction */ but this environment cannot auto-push (fork PR). Run locally: ./scripts/relocate_root_prerelease_ver_to_preproduction.sh" >&2
    exit 1
  fi
  exit 0
fi

d="$(date +%Y-%m-%d)"
moved=0
for f in "${ver_root[@]}"; do
  [[ -f "$f" ]] || continue
  rel="${f#"$ENT"/}"
  [[ "$(dirname "$rel")" == "." ]] || continue
  pr=$(ver_parse_flag_field PRERELEASE "$f")
  (( pr == 1 )) || continue
  gm=$(ver_parse_flag_field GM "$f")
  if (( gm == 1 )); then
    echo "relocate_root_prerelease_ver_to_preproduction: leaving root GM=1 row in place for promotion: $rel"
    continue
  fi

  m=$(ver_parse_nonneg_int_field MAJOR_VERSION "$f")
  [[ -n "$m" ]] || m=$(ver_parse_nonneg_int_field VERSION_MAJOR "$f")
  s=$(ver_parse_nonneg_int_field STANDARD_VERSION "$f")
  [[ -n "$s" ]] || s=$(ver_parse_nonneg_int_field VERSION_STANDARD "$f")
  r=$(ver_parse_nonneg_int_field RELEASE_VERSION "$f")
  [[ -n "$r" ]] || r=$(ver_parse_nonneg_int_field MINOR_VERSION "$f")
  [[ -n "$r" ]] || r=$(ver_parse_nonneg_int_field VERSION_PATCH "$f")
  if [[ -z "$m" || -z "$s" || -z "$r" ]]; then
    echo "relocate_root_prerelease_ver_to_preproduction: missing semver in $f" >&2
    exit 1
  fi

  ver_stamp_release_date_if_missing "$f" "$d"

  dest_dir="$ENT/preproduction ${m}.${s}.${r}"
  bn=$(basename "$f")
  dest="$dest_dir/$bn"
  mkdir -p "$dest_dir"
  if [[ -e "$dest" && "$f" != "$dest" ]]; then
    echo "relocate_root_prerelease_ver_to_preproduction: refuse overwrite — $dest already exists" >&2
    exit 1
  fi
  if [[ "$f" != "$dest" ]]; then
    mv "$f" "$dest"
    moved=1
    echo "relocate_root_prerelease_ver_to_preproduction: $rel -> preproduction ${m}.${s}.${r}/$bn"
  fi
done

if (( moved )); then
  echo "relocate_root_prerelease_ver_to_preproduction: done (moved at least one file)"
else
  echo "relocate_root_prerelease_ver_to_preproduction: nothing to do"
fi

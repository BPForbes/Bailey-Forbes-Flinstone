#!/usr/bin/env bash
# Report version/ tree state for develop→main merge simulations (GM=1 promotion).
# Compare origin/develop vs origin/main from one clone, or inspect a develop worktree
# after setting GM=1 and running promote (use --check and/or --promote-dry-run).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="version_merge_sim_status"

DEVELOP_REF=""
MAIN_REF="origin/main"
CHECK=0
PROMOTE_DRY_RUN=0
JSON=0

usage() {
  cat <<'EOF'
Report version/ file status for develop→main merge simulations (GM=1 promotion).

Usage:
  ./scripts/version_merge_sim_status.sh [options]

Options:
  --root PATH           Repository root for working-tree scan (default: repo containing scripts/)
  --develop REF         Git ref for develop side (e.g. origin/develop); uses git show, no checkout
  --main REF            Git ref for main side (default: origin/main)
  --worktree PATH       Develop checkout path (same as --root PATH; scans files on disk)
  --check               Run check_version_prerelease_layout.sh and
                        check_version_main_prerelease_policy.sh on --root / worktree
  --promote-dry-run     Run promote_preproduction_for_main.sh --dry-run on working tree
  --json                Machine-readable output
  -h, --help

Examples:
  # Compare remote develop vs main without checking out either branch:
  ./scripts/version_merge_sim_status.sh --develop origin/develop --main origin/main

  # After GM=1 on a develop clone (paths + CI-style checks):
  ./scripts/version_merge_sim_status.sh --worktree /path/to/develop-clone --check

  # Preview promote on current checkout:
  ./scripts/version_merge_sim_status.sh --promote-dry-run

  # Develop clone status + dry-run promote + layout/main policy:
  ./scripts/version_merge_sim_status.sh --check --promote-dry-run
EOF
}

die() {
  echo "version_merge_sim_status: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root) ROOT="$2"; shift 2 ;;
    --develop) DEVELOP_REF="$2"; shift 2 ;;
    --main) MAIN_REF="$2"; shift 2 ;;
    --worktree) ROOT="$2"; shift 2 ;;
    --check) CHECK=1; shift ;;
    --promote-dry-run) PROMOTE_DRY_RUN=1; shift ;;
    --json) JSON=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

# JSON string body (no surrounding quotes); escapes newlines and control chars.
json_escape() {
  python3 -c 'import json,sys; print(json.dumps(sys.argv[1])[1:-1], end="")' _ "$1"
}

json_string_array() {
  local -a items=("$@")
  local first=1 s
  printf '['
  for s in "${items[@]}"; do
    (( first )) || printf ','
    first=0
    printf '"%s"' "$(json_escape "$s")"
  done
  printf ']'
}

# ── ref-side scan (git show; no working tree changes) ─────────────────────────

ref_preproduction_dirs() {
  local ref="$1" meta path
  while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    meta="${line%%$'\t'*}"
    path="${line#*$'\t'}"
    [[ "$meta" == *" tree "* ]] || continue
    [[ "$path" == */preproduction\ * ]] && echo "$path"
  done < <(git -C "$ROOT" ls-tree "$ref" "version/entries/" 2>/dev/null || true)
}

ref_ver_paths() {
  local ref="$1"
  git -C "$ROOT" ls-tree -r --name-only "$ref" -- version/entries version/locked 2>/dev/null |
    grep -E '\.ver$' || true
}

ref_gm1_files() {
  local ref="$1" dir rel gm
  while IFS= read -r dir; do
    [[ -n "$dir" ]] || continue
    while IFS= read -r rel; do
      [[ "$rel" == *.ver ]] || continue
      gm=$(
        tmp="$(mktemp)"
        trap 'rm -f "$tmp"' EXIT
        git -C "$ROOT" show "$ref:$rel" >"$tmp" 2>/dev/null || exit 0
        ver_parse_flag_field GM "$tmp"
      ) || continue
      [[ "$gm" == "1" ]] && echo "$rel"
    done < <(git -C "$ROOT" ls-tree -r --name-only "$ref" "$dir" 2>/dev/null || true)
  done < <(ref_preproduction_dirs "$ref")
}

scan_ref_side() {
  local label="$1" ref="$2"
  git -C "$ROOT" rev-parse --verify "$ref" >/dev/null 2>&1 ||
    die "unknown git ref for $label: $ref"

  local -a pp_dirs=() entries_ver=() locked_ver=() gm1=()
  local d p

  while IFS= read -r d; do
    [[ -n "$d" ]] && pp_dirs+=("$d")
  done < <(ref_preproduction_dirs "$ref")

  while IFS= read -r p; do
    [[ -n "$p" ]] || continue
    if [[ "$p" == version/entries/* ]]; then
      entries_ver+=("$p")
    elif [[ "$p" == version/locked/* ]]; then
      locked_ver+=("$p")
    fi
  done < <(ref_ver_paths "$ref")

  while IFS= read -r g; do
    [[ -n "$g" ]] && gm1+=("$g")
  done < <(ref_gm1_files "$ref" 2>/dev/null || true)

  if (( JSON )); then
    printf '"%s":{"ref":"%s","preproduction_dirs":%s,"gm1_files":%s,"entries_ver":%s,"locked_ver":%s}' \
      "$(json_escape "$label")" "$(json_escape "$ref")" \
      "$(json_string_array "${pp_dirs[@]}")" \
      "$(json_string_array "${gm1[@]}")" \
      "$(json_string_array "${entries_ver[@]}")" \
      "$(json_string_array "${locked_ver[@]}")"
  else
    echo "=== $label ($ref) ==="
    echo "preproduction_dirs: ${pp_dirs[*]:-(none)}"
    echo "gm1_files: ${gm1[*]:-(none)}"
    echo "entries_ver_count: ${#entries_ver[@]}"
    echo "locked_ver_count: ${#locked_ver[@]}"
    if ((${#entries_ver[@]} > 0)); then
      printf '  entries: %s\n' "${entries_ver[@]}"
    fi
    if ((${#locked_ver[@]} > 0)); then
      printf '  locked: %s\n' "${locked_ver[@]}"
    fi
    echo ""
  fi
}

# ── working-tree scan ─────────────────────────────────────────────────────────

tree_preproduction_dirs() {
  local base="$1/version/entries"
  [[ -d "$base" ]] || return 0
  find "$base" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -printf '%P\n' 2>/dev/null |
    sed 's/^/version\/entries\//' || true
}

tree_gm1_files() {
  local tree_root="$1"
  local dir vf gm rel
  while IFS= read -r -d '' dir; do
    shopt -s nullglob
    for vf in "$dir"/*.ver; do
      [[ -f "$vf" ]] || continue
      gm=$(ver_parse_flag_field GM "$vf")
      [[ "$gm" == "1" ]] && echo "${vf#"$tree_root"/}"
    done
    shopt -u nullglob
  done < <(find "$tree_root/version/entries" -mindepth 1 -maxdepth 1 -type d -name 'preproduction *' -print0 2>/dev/null || true)
}

scan_tree_side() {
  local label="$1" tree_root="$2"
  local -a pp_dirs=() entries_ver=() locked_ver=() gm1=() porcelain=()
  local p rel

  while IFS= read -r p; do
    [[ -n "$p" ]] && pp_dirs+=("$p")
  done < <(tree_preproduction_dirs "$tree_root")

  while IFS= read -r -d '' f; do
    rel="${f#"$tree_root"/}"
    entries_ver+=("$rel")
  done < <(find "$tree_root/version/entries" -type f -name '*.ver' -print0 2>/dev/null || true)

  while IFS= read -r -d '' f; do
    rel="${f#"$tree_root"/}"
    locked_ver+=("$rel")
  done < <(find "$tree_root/version/locked" -type f -name '*.ver' -print0 2>/dev/null || true)

  while IFS= read -r g; do
    [[ -n "$g" ]] && gm1+=("$g")
  done < <(tree_gm1_files "$tree_root" 2>/dev/null || true)

  if git -C "$tree_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    while IFS= read -r line; do
      [[ -n "$line" ]] && porcelain+=("$line")
    done < <(git -C "$tree_root" status --porcelain -- version version/ userland/shell/version_def.h 2>/dev/null || true)
  fi

  if (( JSON )); then
    printf '"%s":{"root":"%s","preproduction_dirs":%s,"gm1_files":%s,"entries_ver":%s,"locked_ver":%s,"git_porcelain":%s}' \
      "$(json_escape "$label")" "$(json_escape "$tree_root")" \
      "$(json_string_array "${pp_dirs[@]}")" \
      "$(json_string_array "${gm1[@]}")" \
      "$(json_string_array "${entries_ver[@]}")" \
      "$(json_string_array "${locked_ver[@]}")" \
      "$(json_string_array "${porcelain[@]}")"
  else
    echo "=== $label (working tree: $tree_root) ==="
    echo "preproduction_dirs: ${pp_dirs[*]:-(none)}"
    echo "gm1_files: ${gm1[*]:-(none)}"
    echo "entries_ver_count: ${#entries_ver[@]}"
    echo "locked_ver_count: ${#locked_ver[@]}"
    if ((${#porcelain[@]} > 0)); then
      echo "git_porcelain (version/ and version_def.h):"
      printf '  %s\n' "${porcelain[@]}"
    else
      echo "git_porcelain: (clean)"
    fi
    echo ""
  fi
}

run_checks() {
  local tree_root="$1"
  local layout_rc=0 main_rc=0
  local layout_msg="" main_msg=""

  if layout_msg=$(cd "$tree_root" && "$tree_root/scripts/check_version_prerelease_layout.sh" 2>&1); then
    layout_rc=0
  else
    layout_rc=$?
  fi

  if main_msg=$(cd "$tree_root" && "$tree_root/scripts/check_version_main_prerelease_policy.sh" 2>&1); then
    main_rc=0
  else
    main_rc=$?
  fi

  if (( JSON )); then
    printf '"checks":{"layout_ok":%s,"main_policy_ok":%s,"layout":"%s","main_policy":"%s"}' \
      "$([[ $layout_rc -eq 0 ]] && echo true || echo false)" \
      "$([[ $main_rc -eq 0 ]] && echo true || echo false)" \
      "$(json_escape "$layout_msg")" \
      "$(json_escape "$main_msg")"
  else
    echo "=== checks ($tree_root) ==="
    echo "layout: $layout_msg"
    echo "main_policy: $main_msg"
    echo ""
  fi

  return $(( layout_rc != 0 || main_rc != 0 ))
}

run_promote_dry_run() {
  local tree_root="$1"
  local out rc=0
  if out=$(cd "$tree_root" && "$tree_root/scripts/promote_preproduction_for_main.sh" --dry-run 2>&1); then
    rc=0
  else
    rc=$?
  fi
  if (( JSON )); then
    printf '"promote_dry_run":{"ok":%s,"output":"%s"}' \
      "$([[ $rc -eq 0 ]] && echo true || echo false)" \
      "$(json_escape "$out")"
  else
    echo "=== promote dry-run ==="
    echo "$out"
    echo ""
  fi
  return "$rc"
}

# ── main ──────────────────────────────────────────────────────────────────────

check_rc=0
promote_rc=0
need_comma=0

emit_comma() {
  if (( need_comma )); then
    printf ','
  else
    need_comma=1
  fi
}

if (( JSON )); then
  printf '{'
fi

if [[ -n "$DEVELOP_REF" ]]; then
  git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
    die "--develop requires a git repository at $ROOT"
  if (( JSON )); then
    emit_comma
    scan_ref_side "develop" "$DEVELOP_REF"
    emit_comma
    scan_ref_side "main" "$MAIN_REF"
  else
    scan_ref_side "develop" "$DEVELOP_REF"
    scan_ref_side "main" "$MAIN_REF"
  fi
else
  if (( JSON )); then
    emit_comma
    scan_tree_side "develop" "$ROOT"
    if git -C "$ROOT" rev-parse --verify "$MAIN_REF" >/dev/null 2>&1; then
      emit_comma
      scan_ref_side "main" "$MAIN_REF"
    fi
  else
    scan_tree_side "develop" "$ROOT"
    if git -C "$ROOT" rev-parse --verify "$MAIN_REF" >/dev/null 2>&1; then
      scan_ref_side "main" "$MAIN_REF"
    fi
  fi
fi

if (( CHECK )); then
  if (( JSON )); then
    emit_comma
    run_checks "$ROOT" || check_rc=1
  elif ! run_checks "$ROOT"; then
    check_rc=1
  fi
fi

if (( PROMOTE_DRY_RUN )); then
  if (( JSON )); then
    emit_comma
    run_promote_dry_run "$ROOT" || promote_rc=1
  elif ! run_promote_dry_run "$ROOT"; then
    promote_rc=1
  fi
fi

if (( JSON )); then
  printf '}\n'
fi

exit $(( check_rc != 0 || promote_rc != 0 ))

# The form a version/entries file takes in version/locked: a *.ver row loses its RERELEASE line (the
# published record carries no re-release marker; fold_rerelease_entries.sh merges a re-release into
# its original on merge). Every other file and line is unchanged. Shared by finalize_version_locked.sh
# (writes it) and check_version_locked_subset_of_entries.sh (compares against it), so they cannot drift.

# ver_locked_form SRC: prints SRC in its locked form on stdout.
ver_locked_form() {
  local src="$1"
  if [[ "$src" == *.ver ]]; then
    sed -E '/^[[:space:]]*(int[[:space:]]+)?RERELEASE=/d' "$src"
  else
    cat "$src"
  fi
}

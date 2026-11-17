# The form a version/entries file takes in version/locked: a *.ver row's RERELEASE=1 becomes
# RERELEASE=0 (the published copy is the release of record); every other file and line is unchanged.
# Shared by finalize_version_locked.sh (writes it) and check_version_locked_subset_of_entries.sh
# (compares against it), so the two cannot drift.

# ver_locked_form SRC: prints SRC in its locked form on stdout.
ver_locked_form() {
  local src="$1"
  if [[ "$src" == *.ver ]]; then
    sed -E 's/^([[:space:]]*(int[[:space:]]+)?RERELEASE=)1([[:space:]]*)$/\10\3/' "$src"
  else
    cat "$src"
  fi
}

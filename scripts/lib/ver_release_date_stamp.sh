# shellcheck shell=bash
# Sourced by stamp_version_release_date.sh and relocate_root_prerelease_ver_to_preproduction.sh

ver_release_date_key_present() {
  local f=$1
  awk '
    BEGIN { in_h = 0; found = 0 }
    function normalize_record(s,   t) {
      t = s
      gsub(/\r/, "", t)
      gsub(/^[[:space:]]+/, "", t)
      gsub(/[[:space:]]+$/, "", t)
      return t
    }
    {
      t = normalize_record($0)
      if (in_h) {
        if (t == delim) in_h = 0
        next
      }
      if (length(t) == 0 || substr(t, 1, 1) == "#") next
      sk = t
      if (sk ~ /^int[[:space:]]+/) {
        sub(/^int[[:space:]]+/, "", sk)
        sk = normalize_record(sk)
      }
      if (index(sk, "DESCRIPTION<<") == 1) {
        rest = substr(sk, 14)
        delim = normalize_record(rest)
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

ver_stamp_release_date_if_missing() {
  local f=$1 date=$2
  [[ -f "$f" ]] || return 0
  if ver_release_date_key_present "$f"; then
    return 0
  fi
  printf '\nRELEASE_DATE=%s\n' "$date" >>"$f"
}

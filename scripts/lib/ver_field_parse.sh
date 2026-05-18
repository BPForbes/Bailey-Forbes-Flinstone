# Strict parsers for numeric assignment lines in *.ver files.
# Source from scripts under scripts/ and set VER_PARSE_ERR_PREFIX before calling.
: "${VER_PARSE_ERR_PREFIX:=ver_field_parse}"

ver_field_value_raw() {
  local key="$1" file="$2"
  grep -E "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null | head -1 |
    sed -E 's/^[[:space:]]*(int[[:space:]]+)?[^=]+=[[:space:]]*//' || true
}

ver_field_has_key() {
  local key="$1" file="$2"
  grep -qE "^[[:space:]]*(int[[:space:]]+)?${key}=" "$file" 2>/dev/null
}

# Absent key -> empty; present value must match ^[0-9]+$ only (rejects 1abc, 0abc, KEY=).
ver_parse_nonneg_int_field() {
  local key="$1" file="$2"
  local raw
  raw=$(ver_field_value_raw "$key" "$file")
  if [[ -z "$raw" ]]; then
    if ver_field_has_key "$key" "$file"; then
      echo "${VER_PARSE_ERR_PREFIX}: ${key} must be a non-negative integer — $file" >&2
      exit 1
    fi
    return 0
  fi
  if ! [[ "$raw" =~ ^[0-9]+$ ]]; then
    echo "${VER_PARSE_ERR_PREFIX}: ${key} must be a non-negative integer — $file" >&2
    exit 1
  fi
  echo -n "$raw"
}

# Absent key -> 0; present value must be exactly 0 or 1 (rejects 0abc, 1foo, KEY=).
ver_parse_flag_field() {
  local key="$1" file="$2"
  local raw
  raw=$(ver_field_value_raw "$key" "$file")
  if [[ -z "$raw" ]]; then
    if ver_field_has_key "$key" "$file"; then
      echo "${VER_PARSE_ERR_PREFIX}: ${key} must be 0 or 1 — $file" >&2
      exit 1
    fi
    echo -n "0"
    return 0
  fi
  if [[ "$raw" == "0" || "$raw" == "1" ]]; then
    echo -n "$raw"
    return 0
  fi
  echo "${VER_PARSE_ERR_PREFIX}: ${key} must be 0 or 1 — $file" >&2
  exit 1
}

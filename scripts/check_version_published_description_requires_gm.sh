#!/usr/bin/env bash
# PUBLISHED_DESCRIPTION is the portfolio-facing override for DESCRIPTION, read
# only by promote_preproduction_for_main.sh when it promotes a GM=1 row. It may
# only be authored on that same GM=1 row: never on a routine root .ver (GM=1 is
# already forbidden there by check_version_prerelease_layout.sh) and never on a
# preproduction row that is not the GM=1 candidate.
#
# Length is capped here because it is an objective, machine-checkable property;
# whether the text actually and faithfully summarizes DESCRIPTION (no
# contradiction, no unstated claim, no dropped headline point) is a judgment
# call for review, not a shell script — see the "version/**/*.ver" path
# instructions in .coderabbit.yaml and the PUBLISHED_DESCRIPTION section of
# docs/versioning.md and AGENTS.md, which ask CodeRabbit to flag and comment
# on that specific failure mode.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/ver_field_parse.sh
source "$SCRIPT_DIR/lib/ver_field_parse.sh"
VER_PARSE_ERR_PREFIX="check_version_published_description_requires_gm"

# Kept as one named constant so the limit is defined once and every doc that
# quotes a number (docs/versioning.md, AGENTS.md, .coderabbit.yaml) can be
# grepped against this file rather than drifting silently out of sync.
PUBLISHED_DESCRIPTION_MAX_LEN=100

# Mirrors extract_published_description() in promote_preproduction_for_main.sh
# exactly (trim, then strip one layer of matching quotes), so the length
# measured here is the length of the string that will actually reach
# metadata/releases.json — not the raw "KEY=..." line with its quote
# characters still counted. Printed as a character count computed in Python
# rather than bash's `${#value}`, which counts bytes, not characters, unless
# the runtime locale happens to be UTF-8-aware — curated summaries already use
# "·" and other multi-byte separators, so a byte count would reject a
# perfectly compact description depending on $LANG.
published_description_length() {
  python3 - "$1" <<'PY'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8", errors="replace").read().splitlines()
value = ""
for raw in text:
    t = raw.rstrip("\r").strip()
    if not t or t.startswith("#"):
        continue
    sk = t
    if sk.startswith("int "):
        sk = sk[4:].strip()
    if sk.startswith("PUBLISHED_DESCRIPTION="):
        value = sk.split("=", 1)[1].strip()
        if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
            value = value[1:-1]
        break
sys.stdout.write(str(len(value)))
PY
}

err=0

while IFS= read -r -d '' f; do
  if ver_field_has_key PUBLISHED_DESCRIPTION "$f"; then
    gm_val=$(ver_parse_flag_field GM "$f")
    if [[ "$gm_val" != "1" ]]; then
      echo "check_version_published_description_requires_gm: PUBLISHED_DESCRIPTION requires GM=1 on the same row — $f" >&2
      err=1
    fi

    len=$(published_description_length "$f")
    if [[ "$len" -gt $PUBLISHED_DESCRIPTION_MAX_LEN ]]; then
      echo "check_version_published_description_requires_gm: PUBLISHED_DESCRIPTION is ${len} characters, over the ${PUBLISHED_DESCRIPTION_MAX_LEN}-character limit — $f" >&2
      err=1
    fi
  fi
done < <(find "$ROOT/version/entries" "$ROOT/version/locked" -type f -name '*.ver' -print0 2>/dev/null)

if (( err )); then
  exit 1
fi
echo "check_version_published_description_requires_gm: ok"

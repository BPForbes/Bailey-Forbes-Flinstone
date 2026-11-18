#!/usr/bin/env bash
# Fold re-released rows into their original when a branch merges (docs/versioning.md, RERELEASE).
#
# For every version/entries/**/*.ver with RERELEASE=1, the original is the one row with the same
# (MAJOR, STANDARD, RELEASE, DEV_VERSION) whose RERELEASE is 0 or absent. The re-release's
# DESCRIPTION is appended to the original's (after a space on a one-line DESCRIPTION=, as new lines
# before the closing delimiter of a DESCRIPTION<<DELIM block), then the re-released file is deleted
# from version/entries and, when present, from version/locked at the same relative path. The
# original keeps its path, RELEASE_DATE and every other line. Several re-releases fold in path order.
# A re-release with no single original is an error (nothing is changed).
#
# Usage: scripts/fold_rerelease_entries.sh [--dry-run-exit-if-needed]
#   --dry-run-exit-if-needed  change nothing; exit 1 when a fold is pending, 0 when none is.
set -euo pipefail
ROOT="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
mode="apply"
case "${1:-}" in
  "") ;;
  --dry-run-exit-if-needed) mode="dry-run" ;;
  *) echo "usage: $0 [--dry-run-exit-if-needed]" >&2; exit 2 ;;
esac
[[ -d "$ROOT/version/entries" ]] || { echo "fold_rerelease_entries: no version/entries — nothing to fold"; exit 0; }

python3 -I - "$ROOT" "$mode" <<'PY'
import os, re, sys

root, mode = sys.argv[1], sys.argv[2]
ent = os.path.join(root, "version", "entries")
lck = os.path.join(root, "version", "locked")
ALIASES = {
    "m": ("MAJOR_VERSION", "VERSION_MAJOR"),
    "s": ("STANDARD_VERSION", "VERSION_STANDARD"),
    "r": ("RELEASE_VERSION", "MINOR_VERSION", "VERSION_PATCH"),
    "d": ("DEV_VERSION",),
    "rr": ("RERELEASE",),
}

def key_of(line):
    m = re.match(r"^\s*(?:int\s+)?([A-Z_]+)(=|<<)", line)
    return (m.group(1), m.group(2)) if m else (None, None)

def field(lines, names):
    for name in names:
        for line in lines:
            k, op = key_of(line)
            if k == name and op == "=":
                return line.split("=", 1)[1].strip()
    return None

def description(lines, where):
    """(start, end, text): line span of the DESCRIPTION and its text."""
    for i, line in enumerate(lines):
        k, op = key_of(line)
        if k != "DESCRIPTION":
            continue
        if op == "=":
            return i, i, line.split("=", 1)[1].strip()
        delim = line.split("<<", 1)[1].strip()
        for j in range(i + 1, len(lines)):
            if lines[j].strip() == delim:
                return i, j, "\n".join(lines[i + 1:j]).strip()
        sys.exit("fold_rerelease_entries: unterminated DESCRIPTION<<%s in %s" % (delim, where))
    return None

rows = []
for dirpath, _, files in os.walk(ent):
    for f in files:
        if f.endswith(".ver"):
            p = os.path.join(dirpath, f)
            lines = open(p, encoding="utf-8").read().splitlines()
            q = tuple(field(lines, ALIASES[k]) or ("0" if k == "d" else "") for k in "msrd")
            rows.append((p, lines, q, field(lines, ALIASES["rr"]) == "1"))
rows.sort(key=lambda r: r[0])

groups = {}
for row in rows:
    groups.setdefault(row[2], []).append(row)

plans, errors = [], []
for q, members in sorted(groups.items()):
    originals = [r for r in members if not r[3]]
    rereleases = [r for r in members if r[3]]
    if not rereleases:
        continue
    if len(originals) != 1:
        errors.append("%s.%s.%s dev %s: %d originals for %d re-release(s)" % (q + (len(originals), len(rereleases))))
        continue
    plans.append((originals[0], rereleases))

for e in errors:
    print("fold_rerelease_entries: cannot fold " + e, file=sys.stderr)
if errors:
    sys.exit(1)
if mode == "dry-run":
    for orig, rels in plans:
        for r in rels:
            print("fold_rerelease_entries: pending: %s into %s" % (os.path.relpath(r[0], root), os.path.relpath(orig[0], root)))
    if not plans:
        print("fold_rerelease_entries: nothing to fold")
    sys.exit(1 if plans else 0)

for (opath, olines, _, _), rels in plans:
    lines = list(olines)
    for rpath, rlines, _, _ in rels:
        incoming = description(rlines, rpath)
        span = description(lines, opath)
        if not incoming or not incoming[2] or not span:
            sys.exit("fold_rerelease_entries: %s or %s has no DESCRIPTION" % (rpath, opath))
        start, end, _ = span
        if start == end:
            lines[start] = lines[start].rstrip() + " " + " ".join(incoming[2].split())
        else:
            lines[end:end] = incoming[2].splitlines()
        os.remove(rpath)
        rel = os.path.relpath(rpath, ent)
        locked_copy = os.path.join(lck, rel)
        if os.path.isfile(locked_copy):
            os.remove(locked_copy)
        print("fold_rerelease_entries: folded %s into %s" % (os.path.relpath(rpath, root), os.path.relpath(opath, root)))
    with open(opath, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
if not plans:
    print("fold_rerelease_entries: nothing to fold")
PY

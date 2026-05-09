#!/usr/bin/env bash
# Fail if CMakeLists.txt project(... VERSION A.B.C) does not match
# userland/shell/version_def.h (VERSION_MAJOR/STANDARD/PATCH).
# Catches drift between the CMake package version and the shipped shell semver.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEF="$ROOT/userland/shell/version_def.h"
CMK="$ROOT/CMakeLists.txt"

if [[ ! -f "$DEF" || ! -f "$CMK" ]]; then
  echo "error: missing $DEF or $CMK" >&2
  exit 1
fi

pick() {
  local key="$1"
  grep -E "^#define[[:space:]]+${key}[[:space:]]+" "$DEF" | head -1 | awk '{print $3}'
}

ma="$(pick VERSION_MAJOR)"
st="$(pick VERSION_STANDARD)"
pa="$(pick VERSION_PATCH)"
if [[ -z "$ma" || -z "$st" || -z "$pa" ]]; then
  echo "error: could not read VERSION_* from $DEF" >&2
  exit 1
fi
shell_ver="${ma}.${st}.${pa}"

proj_line="$(grep -E '^[[:space:]]*project[[:space:]]*\(' "$CMK" | head -1 || true)"
if [[ -z "$proj_line" ]]; then
  echo "error: no project() line in $CMK" >&2
  exit 1
fi
if [[ ! "$proj_line" =~ VERSION[[:space:]]+([0-9]+)\.([0-9]+)\.([0-9]+) ]]; then
  echo "error: project() in $CMK has no VERSION A.B.C (expected like: project(... VERSION ${shell_ver} ...))" >&2
  exit 1
fi
cmake_ver="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}"

if [[ "$cmake_ver" != "$shell_ver" ]]; then
  echo "error: CMake project VERSION (${cmake_ver}) != shell version_def.h (${shell_ver})" >&2
  echo "Update CMakeLists.txt project(... VERSION ${shell_ver} ...) to match userland/shell/version_def.h" >&2
  exit 1
fi

echo "OK: CMake project VERSION ${cmake_ver} matches version_def.h"

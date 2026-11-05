#!/usr/bin/env bash
# Checks the Lean model and regenerates kernel/core/sched/wq_model_gen.inc.
# Needs Lean 4:  nix shell nixpkgs#lean4 -c formal/lean/build.sh   (Lean 4.35.0 was used)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cd "$here"
lean -o "$tmp/FlWqCode.olean" FlWqCode.lean -c "$tmp/FlWqCode.c"
LEAN_PATH="$tmp" lean FlWq.lean            # every theorem must check: a failure stops here
python3 -I extract_c.py "$tmp/FlWqCode.c" "$here/../../kernel/core/sched/wq_model_gen.inc"
echo "formal: model checked, kernel/core/sched/wq_model_gen.inc regenerated"

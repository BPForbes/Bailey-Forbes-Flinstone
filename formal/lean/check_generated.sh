#!/usr/bin/env bash
# Needs no Lean: the generated fragment records the sha256 of the Lean source it came from; it must match.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
want="$(sha256sum "$here/FlWqCode.lean" | cut -d' ' -f1)"
have="$(sed -n 's/.*source-sha256: \([0-9a-f]\{64\}\).*/\1/p' "$here/../../kernel/core/sched/wq_model_gen.inc" | head -1)"
if [ "$want" != "$have" ]; then
  echo "formal: kernel/core/sched/wq_model_gen.inc is not from the current FlWqCode.lean; run formal/lean/build.sh" >&2
  exit 1
fi
echo "formal: wq_model_gen.inc matches FlWqCode.lean"

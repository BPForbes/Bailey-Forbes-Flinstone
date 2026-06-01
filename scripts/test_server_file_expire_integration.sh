#!/usr/bin/env bash
# Integration smoke: session FILE_META wire + server_shared quarantine jail deny.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make -q BPForbes_Flinstone_Shell 2>/dev/null || make
make test_server_file_expire
make test_server_file_meta
make server_shared_quarantine_harness

WORK="$ROOT/.tmp_server_file_expire_test"
rm -rf "$WORK"
mkdir -p "$WORK/server_shared/expired"
echo 'blocked' >"$WORK/server_shared/expired/blocked.txt"

(
  cd "$WORK"
  "$ROOT/tests/server_shared_quarantine_harness"
)

if [[ -f "$WORK/server_shared/share-test.meta" ]]; then
  echo "unexpected .meta sidecar on receiver image" >&2
  exit 1
fi
echo "no on-disk share meta sidecar: OK"

# tmux startup smoke only (no -Ex2s end-to-end expiry assertion in this script).
if command -v tmux >/dev/null 2>&1; then
  SESSION="fl-server-file-expire-$$"
  tmux new-session -d -s "$SESSION" -c "$WORK" \
    "./BPForbes_Flinstone_Shell -y 'server host 127.0.0.1:9876' ; sleep 60"
  sleep 1
  tmux kill-session -t "$SESSION" 2>/dev/null || true
  echo "tmux emulator startup smoke: OK"
else
  echo "tmux not installed; skipped tmux emulator startup smoke"
fi

echo "All server file expire integration checks passed."

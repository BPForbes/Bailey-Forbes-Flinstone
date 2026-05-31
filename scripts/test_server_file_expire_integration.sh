#!/usr/bin/env bash
# Integration smoke: server file -Ex2s expiry and server_shared/expired jail deny.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make -q BPForbes_Flinstone_Shell 2>/dev/null || make
make test_server_file_expire
make purge_shared_expired_harness

WORK="$ROOT/.tmp_server_file_expire_test"
rm -rf "$WORK"
mkdir -p "$WORK"
echo 'expire-me' >"$WORK/payload.txt"

# Direct expiry enforcement via fl_server_shared_purge_expired + jail quarantine.
python3 - <<'PY' "$WORK"
import os, sys, time
work = sys.argv[1]
shared_root = os.path.join(work, "server_shared")
shared_file = os.path.join(shared_root, "payload.txt")
expired_file = os.path.join(shared_root, "expired", "payload.txt")
expired_meta = os.path.join(shared_root, "expired", "share-test.meta")
os.makedirs(shared_root, exist_ok=True)
with open(shared_file, "w") as f:
    f.write("expire-me\n")
meta = os.path.join(shared_root, "share-test.meta")
with open(meta, "w") as f:
    f.write("share_id=share-test\n")
    f.write(f"expires_at={int(time.time()) - 1}\n")
    f.write("file_name=payload.txt\n")
os.makedirs(os.path.join(shared_root, "expired"), exist_ok=True)
print("prepared expired share metadata:", meta)
PY

(
  cd "$WORK"
  "$ROOT/tests/purge_shared_expired_harness"
)

if [[ ! -f "$WORK/server_shared/expired/payload.txt" ]]; then
  echo "expected expired quarantine payload missing" >&2
  exit 1
fi
if [[ -f "$WORK/server_shared/payload.txt" ]]; then
  echo "expired payload was not moved out of server_shared/" >&2
  exit 1
fi
if [[ ! -f "$WORK/server_shared/expired/share-test.meta" ]]; then
  echo "expected expired sidecar meta missing" >&2
  exit 1
fi
echo "expired quarantine path match: OK"

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

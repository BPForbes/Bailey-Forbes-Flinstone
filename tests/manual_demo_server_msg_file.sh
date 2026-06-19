#!/bin/bash
# tmux demo: server messaging + native file share (host + sender + receiver).
# Captures pane scrollback to /opt/cursor/artifacts/demo_msg_file_*.{ansi,txt}
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || (cd "$SCRIPT_DIR/.." && pwd))"
cd "$REPO_ROOT"

export FL_USERS_LAB_DEFAULTS=1
make -q BPForbes_Flinstone_Shell 2>/dev/null || make BPForbes_Flinstone_Shell

WORK="$REPO_ROOT/.tmp_msg_file_demo"
QUOTE='The road goes ever on and on, down from the door where it began.'
rm -rf "$WORK" server_shared
mkdir -p "$WORK"
printf '%s\n' "$QUOTE" >"$WORK/demo_share.txt"
echo 'Public bulletin for all members.' >"$WORK/public_note.txt"

# One process creates drive.img before parallel interactive shells open it.
./BPForbes_Flinstone_Shell -y whoami >/dev/null 2>&1 || true

T="tmux -f /exec-daemon/tmux.portal.conf"
S=server-msg-file-demo
PORT=${PORT:-49917}

$T has-session -t "=$S" 2>/dev/null && $T kill-session -t "$S"
$T new-session -d -s "$S" -c "$REPO_ROOT" -x 260 -y 72 -- bash -l
sleep 0.5
$T split-window -h -t "$S:0" -c "$REPO_ROOT"
$T split-window -v -t "$S:0.0" -c "$REPO_ROOT"
sleep 0.3
$T select-layout -t "$S:0" tiled

# Launch shells sequentially so drive.img + driver init do not race.
$T send-keys -t "$S:0.0" "clear; export FL_USERS_LAB_DEFAULTS=1; ./BPForbes_Flinstone_Shell whoami" C-m
sleep 3
$T send-keys -t "$S:0.1" "clear; export FL_USERS_LAB_DEFAULTS=1; ./BPForbes_Flinstone_Shell whoami" C-m
sleep 3
$T send-keys -t "$S:0.2" "clear; export FL_USERS_LAB_DEFAULTS=1; ./BPForbes_Flinstone_Shell whoami" C-m
sleep 3

# Pane 0: host
$T send-keys -t "$S:0.0" "server host 127.0.0.1:$PORT" C-m
sleep 1.5

# Panes 1/2: clients (decline nick prompts)
$T send-keys -t "$S:0.1" "server join 127.0.0.1:$PORT" C-m
sleep 1
$T send-keys -t "$S:0.1" "n" C-m
sleep 1
$T send-keys -t "$S:0.2" "server join 127.0.0.1:$PORT" C-m
sleep 1
$T send-keys -t "$S:0.2" "n" C-m
sleep 1.5

# Host assigns readable nicks for targeted msg/file routing.
$T send-keys -t "$S:0.0" "server nick -id 2 -name Alice" C-m
sleep 0.6
$T send-keys -t "$S:0.0" "server nick -id 3 -name Bob" C-m
sleep 0.6
$T send-keys -t "$S:0.0" "server connected" C-m
sleep 0.6

# --- Messaging ---
$T send-keys -t "$S:0.0" "server announce messaging + file share demo" C-m
sleep 0.8
$T send-keys -t "$S:0.1" "server msg -all Hello everyone from Alice" C-m
sleep 0.8
$T send-keys -t "$S:0.1" "server msg -user Bob Private ping before file offer" C-m
sleep 1
$T send-keys -t "$S:0.2" "server msg -user Alice Got your ping" C-m
sleep 1

# --- Private file offer Alice -> Bob ---
$T send-keys -t "$S:0.1" "cat $WORK/demo_share.txt" C-m
sleep 0.6
$T send-keys -t "$S:0.1" "server file -user Bob $WORK/demo_share.txt -vw" C-m
sleep 5
$T send-keys -t "$S:0.2" "server file inbox" C-m
sleep 1.2

INBOX_SNAP="$(mktemp)"
$T capture-pane -t "$S:0.2" -S -120 -p >"$INBOX_SNAP"
SHARE_ID="$(grep -oE 'share-[0-9]+-[0-9]+' "$INBOX_SNAP" | head -1 || true)"
rm -f "$INBOX_SNAP"
if [[ -n "${SHARE_ID:-}" ]]; then
    $T send-keys -t "$S:0.2" "server file accept ${SHARE_ID} --server-share" C-m
    sleep 3
    $T send-keys -t "$S:0.2" "cat server_shared/${SHARE_ID}_demo_share.txt" C-m
    sleep 0.8
    if [[ -f "$REPO_ROOT/server_shared/${SHARE_ID}.meta" ]]; then
        echo "FAIL: meta sidecar persisted on receiver image" >&2
        exit 1
    fi
else
    $T send-keys -t "$S:0.2" "echo WARN_no_share_id_in_inbox" C-m
    sleep 0.5
fi

# --- Public file offer (host) ---
$T send-keys -t "$S:0.0" "server file -all $WORK/public_note.txt -vw" C-m
sleep 1.5
$T send-keys -t "$S:0.2" "server file public" C-m
sleep 0.8
$T send-keys -t "$S:0.1" "server file sent" C-m
sleep 0.8

mkdir -p /opt/cursor/artifacts
for entry in "0:host" "1:alice" "2:bob"; do
    p="${entry%%:*}"
    n="${entry##*:}"
    $T capture-pane -t "$S:0.$p" -S -300 -p -e > "/opt/cursor/artifacts/demo_msg_file_${n}.ansi"
    $T capture-pane -t "$S:0.$p" -S -300 -p    > "/opt/cursor/artifacts/demo_msg_file_${n}.txt"
done

$T send-keys -t "$S:0.0" "server kill" C-m
sleep 0.3
for pane in 0 1 2; do
    $T send-keys -t "$S:0.$pane" "exit" C-m
done
sleep 0.5
$T kill-session -t "$S" 2>/dev/null || true

echo "captured /opt/cursor/artifacts/demo_msg_file_{host,alice,bob}.{ansi,txt}"
if [[ -z "${SHARE_ID:-}" ]]; then
    echo "WARN: file accept step skipped (share id not found in Bob inbox)" >&2
    exit 1
fi
echo "accepted share: ${SHARE_ID}"
if ! grep -Fq "$QUOTE" /opt/cursor/artifacts/demo_msg_file_alice.txt; then
    echo "FAIL: sender pane does not show shared file quote" >&2
    exit 1
fi
if ! grep -Fq "$QUOTE" /opt/cursor/artifacts/demo_msg_file_bob.txt; then
    echo "FAIL: receiver pane does not show shared file quote after accept" >&2
    exit 1
fi
echo "quote visible on sender and receiver: OK"

#!/usr/bin/env bash
# Capture a pcap of Alice -> host -> Bob native file share (FILE_OFFER/META/CHUNK/DONE).
# Writes /opt/cursor/artifacts/server_file_exchange.{pcap,txt,frames.txt}
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || (cd "$SCRIPT_DIR/.." && pwd))"
cd "$REPO_ROOT"

ART="/opt/cursor/artifacts"
PCAP="$ART/server_file_exchange.pcap"
TIMELINE="$ART/server_file_exchange.txt"
FRAMES="$ART/server_file_exchange_frames.txt"

command -v tcpdump >/dev/null 2>&1 || { echo "tcpdump required" >&2; exit 1; }
command -v tmux >/dev/null 2>&1 || { echo "tmux required" >&2; exit 1; }

export FL_USERS_LAB_DEFAULTS=1
make -q BPForbes_Flinstone_Shell 2>/dev/null || make BPForbes_Flinstone_Shell

WORK="$REPO_ROOT/.tmp_file_exchange_pcap"
QUOTE='The road goes ever on and on, down from the door where it began.'
rm -rf "$WORK" server_shared
mkdir -p "$WORK" "$ART"
printf '%s\n' "$QUOTE" >"$WORK/joke.txt"

./BPForbes_Flinstone_Shell -y whoami >/dev/null 2>&1 || true

T="tmux -f /exec-daemon/tmux.portal.conf"
S=server-file-pcap-demo
PORT=${PORT:-49918}

$T has-session -t "=$S" 2>/dev/null && $T kill-session -t "$S"

rm -f "$PCAP" "$TIMELINE" "$FRAMES"

# Capture loopback TCP for the hub port (host + two clients).
sudo tcpdump -i lo -U -w "$PCAP" "tcp port $PORT" >/dev/null 2>&1 &
TCPDUMP_PID=$!
cleanup() {
    sudo kill "$TCPDUMP_PID" 2>/dev/null || true
    wait "$TCPDUMP_PID" 2>/dev/null || true
}
trap cleanup EXIT
sleep 0.4

$T new-session -d -s "$S" -c "$REPO_ROOT" -x 220 -y 48 -- bash -l
sleep 0.4
$T split-window -h -t "$S:0" -c "$REPO_ROOT"
$T split-window -v -t "$S:0.0" -c "$REPO_ROOT"
$T select-layout -t "$S:0" tiled

for pane in 0 1 2; do
    $T send-keys -t "$S:0.$pane" "clear; export FL_USERS_LAB_DEFAULTS=1; ./BPForbes_Flinstone_Shell whoami" C-m
    sleep 2.5
done

$T send-keys -t "$S:0.0" "server host 127.0.0.1:$PORT" C-m
sleep 1.2
$T send-keys -t "$S:0.1" "server join 127.0.0.1:$PORT" C-m
sleep 0.8
$T send-keys -t "$S:0.1" "n" C-m
sleep 0.8
$T send-keys -t "$S:0.2" "server join 127.0.0.1:$PORT" C-m
sleep 0.8
$T send-keys -t "$S:0.2" "n" C-m
sleep 1

$T send-keys -t "$S:0.0" "server nick -id 2 -name Alice" C-m
sleep 0.5
$T send-keys -t "$S:0.0" "server nick -id 3 -name Bob" C-m
sleep 0.5

# File exchange: Sarah/Alice sends joke.txt to Bob.
$T send-keys -t "$S:0.1" "server file -user Bob $WORK/joke.txt -vw" C-m
sleep 5
$T send-keys -t "$S:0.2" "server file inbox" C-m
sleep 1

INBOX_SNAP="$(mktemp)"
$T capture-pane -t "$S:0.2" -S -80 -p >"$INBOX_SNAP"
SHARE_ID="$(grep -oE 'share-[0-9]+-[0-9]+' "$INBOX_SNAP" | head -1 || true)"
rm -f "$INBOX_SNAP"

if [[ -n "${SHARE_ID:-}" ]]; then
    $T send-keys -t "$S:0.2" "server file accept ${SHARE_ID} --server-share" C-m
    sleep 2
    $T send-keys -t "$S:0.2" "cat server_shared/joke.txt" C-m
    sleep 0.6
fi

mkdir -p "$ART"
for entry in "0:host" "1:alice" "2:bob"; do
    p="${entry%%:*}"
    n="${entry##*:}"
    $T capture-pane -t "$S:0.$p" -S -200 -p -e > "$ART/demo_file_exchange_${n}.ansi" 2>/dev/null || true
    $T capture-pane -t "$S:0.$p" -S -200 -p    > "$ART/demo_file_exchange_${n}.txt" 2>/dev/null || true
done

$T send-keys -t "$S:0.0" "server kill" C-m
sleep 0.3
for pane in 0 1 2; do
    $T send-keys -t "$S:0.$pane" "exit" C-m
done
sleep 0.5
$T kill-session -t "$S" 2>/dev/null || true

sleep 0.5
cleanup
trap - EXIT

if [[ ! -s "$PCAP" ]]; then
    echo "pcap missing or empty: $PCAP" >&2
    exit 1
fi

sudo tcpdump -r "$PCAP" -n -tttt >"$TIMELINE" 2>/dev/null || tcpdump -r "$PCAP" -n -tttt >"$TIMELINE"

python3 tests/decode_session_pcap.py "$PCAP" --counts >"$FRAMES" 2>&1 || true

echo "pcap:     $PCAP ($(stat -c %s "$PCAP") bytes)"
echo "timeline: $TIMELINE"
echo "frames:   $FRAMES"
if [[ -n "${SHARE_ID:-}" ]]; then
    echo "share_id: $SHARE_ID"
fi

python3 tests/render_file_exchange_pcap.py "$PCAP" "$FRAMES" "${SHARE_ID:-share-unknown}"

echo "All server file exchange capture artifacts written under $ART"

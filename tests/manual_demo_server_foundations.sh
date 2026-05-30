#!/bin/bash
# Drives a complete server foundations demo using 3 BPForbes_Flinstone_Shell
# instances under tmux. Captures each pane's content to /opt/cursor/artifacts.
set -e
# Resolve repo root from the script location (preferring git when
# available) so the demo runs from any CWD.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || (cd "$SCRIPT_DIR/.." && pwd))"
if [ -z "$REPO_ROOT" ] || [ ! -d "$REPO_ROOT" ]; then
    echo "manual_demo_server_foundations.sh: could not resolve repo root" >&2
    exit 1
fi
cd "$REPO_ROOT"

T="tmux -f /exec-daemon/tmux.portal.conf"
S=server-demo-recap
PORT=${PORT:-49913}

$T has-session -t "=$S" 2>/dev/null && $T kill-session -t "$S"
$T new-session -d -s "$S" -c "$PWD" -x 240 -y 60 -- bash -l
sleep 1
$T split-window -h -t "$S:0" -c "$PWD"
$T split-window -v -t "$S:0.0" -c "$PWD"
sleep 0.5

# Launch shells (use "whoami" arg trick so they enter interactive mode).
for pane in 0 1 2; do
    $T send-keys -t "$S:0.$pane" "clear; ./BPForbes_Flinstone_Shell whoami" C-m
done
sleep 2

# 1. Host starts the session.
$T send-keys -t "$S:0.0" "server host 127.0.0.1:$PORT" C-m
sleep 1

# 2. Both clients join (both render as 'flinstone' so we get collisions).
$T send-keys -t "$S:0.1" "server join 127.0.0.1:$PORT" C-m
sleep 1
$T send-keys -t "$S:0.2" "server join 127.0.0.1:$PORT" C-m
sleep 1

# 3. Host announces.
$T send-keys -t "$S:0.0" "server announce welcome to the lab" C-m
sleep 1

# 4. Host nicks member 2 to "Jeff".
$T send-keys -t "$S:0.0" "server nick -id 2 -name Jeff" C-m
sleep 1

# 5. Host attempts collision: nick member 3 to existing principal "flinstone".
$T send-keys -t "$S:0.0" "server nick -id 3 -name flinstone" C-m
sleep 1

# 6. Host roster.
$T send-keys -t "$S:0.0" "server connected" C-m
sleep 1

# 7. Client A leaves.
$T send-keys -t "$S:0.1" "server leave" C-m
sleep 1

# Capture each pane WITH full scrollback (-S -200) so earlier announcements
# stay in the artifact even after later ones scroll the visible region.
mkdir -p /opt/cursor/artifacts
$T capture-pane -t "$S:0.0" -S -200 -p -e > /opt/cursor/artifacts/demo_host_pane.ansi
$T capture-pane -t "$S:0.1" -S -200 -p -e > /opt/cursor/artifacts/demo_clientA_pane.ansi
$T capture-pane -t "$S:0.2" -S -200 -p -e > /opt/cursor/artifacts/demo_clientB_pane.ansi
$T capture-pane -t "$S:0.0" -S -200 -p > /opt/cursor/artifacts/demo_host_pane.txt
$T capture-pane -t "$S:0.1" -S -200 -p > /opt/cursor/artifacts/demo_clientA_pane.txt
$T capture-pane -t "$S:0.2" -S -200 -p > /opt/cursor/artifacts/demo_clientB_pane.txt

# Tear down.
$T send-keys -t "$S:0.0" "server kill" C-m
sleep 0.3
for pane in 0 1 2; do
    $T send-keys -t "$S:0.$pane" "exit" C-m
done
sleep 0.5
$T kill-session -t "$S" 2>/dev/null || true

echo "captured to /opt/cursor/artifacts/demo_*.{ansi,txt}"

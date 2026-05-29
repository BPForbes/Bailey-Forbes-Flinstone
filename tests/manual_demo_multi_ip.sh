#!/bin/bash
# Multi-IP server foundations demo.
#
# Brings up four BPForbes_Flinstone_Shell instances in a tmux session
# (1 host + 3 clients) where every process sources its TCP traffic from a
# distinct 10.99.0.X loopback alias. The host does NOT bind on 127.0.0.1.
#
# Requires the 10.99.0.0/24 aliases on `lo`:
#   sudo ip addr add 10.99.0.1/24  dev lo
#   sudo ip addr add 10.99.0.10/24 dev lo
#   sudo ip addr add 10.99.0.20/24 dev lo
#   sudo ip addr add 10.99.0.30/24 dev lo
#
# Captures every pane's ANSI buffer to /opt/cursor/artifacts/ for review.

set -e
cd /workspace

T="tmux -f /exec-daemon/tmux.portal.conf"
S=server-multi-ip-demo
HOST_IP=${HOST_IP:-10.99.0.1}
A_IP=${A_IP:-10.99.0.10}
B_IP=${B_IP:-10.99.0.20}
C_IP=${C_IP:-10.99.0.30}
PORT=${PORT:-49913}

$T has-session -t "=$S" 2>/dev/null && $T kill-session -t "$S"
$T new-session -d -s "$S" -c "$PWD" -x 260 -y 60 -- bash -l
sleep 1
# 4-way split: top-left host, top-right A, bottom-left B, bottom-right C.
$T split-window -h -t "$S:0"   -c "$PWD"
$T split-window -v -t "$S:0.0" -c "$PWD"
$T split-window -v -t "$S:0.2" -c "$PWD"
sleep 0.5
$T select-layout -t "$S:0" tiled
sleep 0.3

# Launch four shells (use "whoami" arg trick so they enter interactive mode).
for pane in 0 1 2 3; do
    $T send-keys -t "$S:0.$pane" "clear; ./BPForbes_Flinstone_Shell whoami" C-m
done
sleep 2.5

# Pane 0: Host on 10.99.0.1:PORT (NOT loopback).
$T send-keys -t "$S:0.0" "server host ${HOST_IP}:${PORT}" C-m
sleep 1

# Pane 1: client A on 10.99.0.10, joins host. Since host is also 'flinstone'
# the join triggers the Y/N nick prompt — A declines ("n") and keeps the
# disambiguated principal.
$T send-keys -t "$S:0.1" "server join ${HOST_IP}:${PORT} -bind ${A_IP}" C-m
sleep 0.8
$T send-keys -t "$S:0.1" "n" C-m
sleep 0.8

# Pane 2: client B on 10.99.0.20, also joins as "flinstone" -> NICK_PROMPT.
# Sequence: server join + answer y + nick "Bobby".
$T send-keys -t "$S:0.2" "server join ${HOST_IP}:${PORT} -bind ${B_IP}" C-m
sleep 0.8
$T send-keys -t "$S:0.2" "y" C-m
sleep 0.4
$T send-keys -t "$S:0.2" "Bobby" C-m
sleep 1

# Pane 3: client C on 10.99.0.30, joins; declines the nick prompt too.
$T send-keys -t "$S:0.3" "server join ${HOST_IP}:${PORT} -bind ${C_IP}" C-m
sleep 0.8
$T send-keys -t "$S:0.3" "n" C-m
sleep 1

# Roster from each side (anyone can run it now).
$T send-keys -t "$S:0.0" "server connected" C-m
sleep 0.3
$T send-keys -t "$S:0.1" "server connected" C-m
sleep 0.3
$T send-keys -t "$S:0.3" "server connected" C-m
sleep 1

# Host announcement.
$T send-keys -t "$S:0.0" "server announce welcome to the lab" C-m
sleep 0.6

# Global broadcast from A.
$T send-keys -t "$S:0.1" "server msg Hi everyone!" C-m
sleep 0.6

# Private message: A -> Bobby (the host-global nick on slot 3).
$T send-keys -t "$S:0.1" "server msg -user Bobby Hello private" C-m
sleep 0.6

# Private message reply: Bobby -> flinstone {2} (A). Three "flinstone"
# principals are connected (host + A + C) so we disambiguate with -id 2.
$T send-keys -t "$S:0.2" "server msg -user flinstone -id 2 Hi back" C-m
sleep 0.6

# Standard shell command still works while in a server session.
$T send-keys -t "$S:0.1" "whoami" C-m
sleep 0.4

# Client C leaves.
$T send-keys -t "$S:0.3" "server leave" C-m
sleep 1

# Bobby (client B) leaves, then runs whoami: the host-global nick "Bobby"
# is a SESSION-only label; the shell login must still be the real shell
# user ("flinstone"). This is the test the user asked for in the follow-up.
$T send-keys -t "$S:0.2" "server leave" C-m
sleep 0.5
$T send-keys -t "$S:0.2" "whoami" C-m
sleep 0.5

# Host leaves (transfer to next member).
$T send-keys -t "$S:0.0" "server leave" C-m
sleep 1.5

# Capture each pane WITH scrollback so all the events appear in the file.
mkdir -p /opt/cursor/artifacts
for pane in 0 1 2 3; do
    name=$(echo "host A B C" | awk -v i=$((pane + 1)) '{print $i}')
    $T capture-pane -t "$S:0.$pane" -S -300 -p -e > /opt/cursor/artifacts/demo_multi_ip_${name}.ansi
    $T capture-pane -t "$S:0.$pane" -S -300 -p    > /opt/cursor/artifacts/demo_multi_ip_${name}.txt
done

# Tear down.
for pane in 0 1 2 3; do
    $T send-keys -t "$S:0.$pane" "exit" C-m
done
sleep 0.8
$T kill-session -t "$S" 2>/dev/null || true

echo "captured /opt/cursor/artifacts/demo_multi_ip_{host,A,B,C}.{ansi,txt}"

#!/bin/bash
#
# tests/manual_demo_netns_pcap.sh
#
# Cross-subnet (multi-network) end-to-end demo of the P3-13 server with a
# packet capture taken on the router namespace. Topology:
#
#   netA (192.168.10.0/24)              netB (192.168.20.0/24)
#   fl_host    192.168.10.2 ---|       |--- fl_client 192.168.20.2
#                              br-a   br-b
#                                |    |
#                            fl_router (192.168.10.1 / 192.168.20.1,
#                                       net.ipv4.ip_forward=1)
#
# Requires:
#   - sudo (passwordless or interactive)
#   - iproute2 (ip), tcpdump
#   - net.bridge.bridge-nf-call-iptables sysctls writable
#   - tmux (for the multi-pane shell drive)
#   - optional: scapy (`pip install scapy`) for the per-frame decode artifact
#
# Skips cleanly with status 0 when any of the above is missing so this can be
# wired into `make test_netns_pcap` (gated by FL_NETNS_PCAP_OK=1) without
# breaking the default test sweep.
#
# Produces (idempotent):
#   /opt/cursor/artifacts/netns_router_capture.pcap
#       Raw frames crossing the router; openable in Wireshark.
#   /opt/cursor/artifacts/netns_router_capture.txt
#       Human-readable tcpdump timeline.
#   /opt/cursor/artifacts/netns_router_session_frames.txt
#       Decoded Flinstone session-frame view (magic/ver/opcode/length/payload).
#   /opt/cursor/artifacts/netns_demo_host.txt
#   /opt/cursor/artifacts/netns_demo_client.txt
#       Captured tmux panes for the host (192.168.10.2) and client
#       (192.168.20.2) shells driving the session.

set -e
cd "$(dirname "$0")/.."

T="tmux -f /exec-daemon/tmux.portal.conf"
S=fl-netns-pcap
PORT=${PORT:-49913}
ART=/opt/cursor/artifacts
mkdir -p "$ART"

skip() {
    echo "[manual_demo_netns_pcap] skip: $*"
    exit 0
}

# Capability probes
command -v sudo    >/dev/null 2>&1 || skip "sudo not available"
sudo -n true       >/dev/null 2>&1 || skip "passwordless sudo not configured"
command -v ip      >/dev/null 2>&1 || skip "iproute2 (ip) not installed"
command -v tcpdump >/dev/null 2>&1 || skip "tcpdump not installed"
command -v tmux    >/dev/null 2>&1 || skip "tmux not installed"

cleanup() {
    [ -n "${TCPDUMP_PID:-}" ] && sudo kill "$TCPDUMP_PID" 2>/dev/null || true
    $T kill-session -t "$S" 2>/dev/null || true
    # Restore the bridge netfilter sysctls we toggled in setup so the host
    # is not left in a modified firewalling state after an opted-in run.
    [ -n "${ORIG_BR_NF_IPTABLES:-}" ]  && sudo sysctl -w net.bridge.bridge-nf-call-iptables="$ORIG_BR_NF_IPTABLES"  >/dev/null 2>&1 || true
    [ -n "${ORIG_BR_NF_IP6TABLES:-}" ] && sudo sysctl -w net.bridge.bridge-nf-call-ip6tables="$ORIG_BR_NF_IP6TABLES" >/dev/null 2>&1 || true
    [ -n "${ORIG_BR_NF_ARPTABLES:-}" ] && sudo sysctl -w net.bridge.bridge-nf-call-arptables="$ORIG_BR_NF_ARPTABLES" >/dev/null 2>&1 || true
    sudo ip link del br-a 2>/dev/null || true
    sudo ip link del br-b 2>/dev/null || true
    sudo ip netns del fl_router 2>/dev/null || true
    sudo ip netns del fl_host   2>/dev/null || true
    sudo ip netns del fl_client 2>/dev/null || true
}
trap cleanup EXIT
cleanup

# Bridge netfilter is on by default on most kernels and silently drops
# bridged traffic that has no iptables rule to accept it. Capture the
# original values so cleanup() can restore them, then disable per-bridge
# netfilter for the duration of the test.
ORIG_BR_NF_IPTABLES=$(sudo sysctl -n  net.bridge.bridge-nf-call-iptables  2>/dev/null || echo)
ORIG_BR_NF_IP6TABLES=$(sudo sysctl -n net.bridge.bridge-nf-call-ip6tables 2>/dev/null || echo)
ORIG_BR_NF_ARPTABLES=$(sudo sysctl -n net.bridge.bridge-nf-call-arptables 2>/dev/null || echo)
sudo sysctl -w net.bridge.bridge-nf-call-iptables=0  >/dev/null
sudo sysctl -w net.bridge.bridge-nf-call-ip6tables=0 >/dev/null
sudo sysctl -w net.bridge.bridge-nf-call-arptables=0 >/dev/null

# -- topology ---------------------------------------------------------------
for ns in fl_host fl_client fl_router; do sudo ip netns add "$ns"; done
sudo ip link add br-a type bridge stp_state 0 forward_delay 0
sudo ip link add br-b type bridge stp_state 0 forward_delay 0
sudo ip link set br-a up; sudo ip link set br-b up

# fl_host attaches to netA bridge
sudo ip link add a-host type veth peer name a-br
sudo ip link set a-host netns fl_host
sudo ip link set a-br master br-a; sudo ip link set a-br up

# fl_router straddles both bridges
sudo ip link add r-a type veth peer name a-r
sudo ip link add r-b type veth peer name b-r
sudo ip link set r-a netns fl_router
sudo ip link set r-b netns fl_router
sudo ip link set a-r master br-a; sudo ip link set a-r up
sudo ip link set b-r master br-b; sudo ip link set b-r up

# fl_client attaches to netB bridge
sudo ip link add b-client type veth peer name b-br
sudo ip link set b-client netns fl_client
sudo ip link set b-br master br-b; sudo ip link set b-br up

# Addresses + routes + forwarding
sudo ip netns exec fl_host   sh -c "
    ip link set lo up
    ip addr add 192.168.10.2/24 dev a-host
    ip link set a-host up
    ip route add default via 192.168.10.1"
sudo ip netns exec fl_client sh -c "
    ip link set lo up
    ip addr add 192.168.20.2/24 dev b-client
    ip link set b-client up
    ip route add default via 192.168.20.1"
sudo ip netns exec fl_router sh -c "
    ip link set lo up
    ip addr add 192.168.10.1/24 dev r-a; ip link set r-a up
    ip addr add 192.168.20.1/24 dev r-b; ip link set r-b up
    sysctl -w net.ipv4.ip_forward=1 >/dev/null"

# -- sanity: ping must traverse the router ---------------------------------
sudo ip netns exec fl_host ping -c 1 -W 1 192.168.20.2 >/dev/null 2>&1 \
    || skip "cross-subnet ping failed; router not forwarding (check bridge-nf sysctls)"

# -- pcap on the router (sees every cross-subnet frame) --------------------
rm -f "$ART/netns_router_capture.pcap" \
      "$ART/netns_router_capture.txt" \
      "$ART/netns_router_session_frames.txt" \
      "$ART/netns_demo_host.txt" "$ART/netns_demo_client.txt"
sudo ip netns exec fl_router \
    tcpdump -i any -U -w "$ART/netns_router_capture.pcap" \
            "tcp port $PORT" >/dev/null 2>&1 &
TCPDUMP_PID=$!
sleep 0.4

# -- run host + client in tmux ---------------------------------------------
$T new-session -d -s "$S" -c "$PWD" -x 200 -y 40 -- bash -l
sleep 1
$T split-window -h -t "$S:0" -c "$PWD"
sleep 0.3
$T send-keys -t "$S:0.0" "sudo ip netns exec fl_host   ./BPForbes_Flinstone_Shell whoami" C-m
$T send-keys -t "$S:0.1" "sudo ip netns exec fl_client ./BPForbes_Flinstone_Shell whoami" C-m
sleep 3
$T send-keys -t "$S:0.0" "server host 192.168.10.2:${PORT}" C-m
sleep 1
$T send-keys -t "$S:0.1" "server join 192.168.10.2:${PORT}" C-m
sleep 0.8
$T send-keys -t "$S:0.1" "n" C-m         # decline the Y/N nick prompt
sleep 0.6
$T send-keys -t "$S:0.0" "server announce hello from netA" C-m
sleep 0.6
$T send-keys -t "$S:0.1" "server msg cross-subnet broadcast" C-m
sleep 0.6
$T send-keys -t "$S:0.1" "server leave" C-m
sleep 0.5
$T send-keys -t "$S:0.0" "server leave" C-m
sleep 1

# Pane captures (plain text for the PR walkthrough)
$T capture-pane -t "$S:0.0" -S -200 -p > "$ART/netns_demo_host.txt"
$T capture-pane -t "$S:0.1" -S -200 -p > "$ART/netns_demo_client.txt"

# -- stop tcpdump and post-process -----------------------------------------
sudo kill "$TCPDUMP_PID" 2>/dev/null || true
wait "$TCPDUMP_PID" 2>/dev/null || true
TCPDUMP_PID=""

# Human-readable timeline
sudo tcpdump -r "$ART/netns_router_capture.pcap" -n -tttt \
    > "$ART/netns_router_capture.txt" 2>/dev/null

# Session-level decode (skips gracefully when scapy is absent)
python3 tests/decode_session_pcap.py \
    "$ART/netns_router_capture.pcap" --counts \
    > "$ART/netns_router_session_frames.txt"

# -- summary ----------------------------------------------------------------
echo "captured:"
echo "  $ART/netns_router_capture.pcap            $(stat -c %s "$ART/netns_router_capture.pcap") bytes"
echo "  $ART/netns_router_capture.txt             $(wc -l < "$ART/netns_router_capture.txt") lines"
echo "  $ART/netns_router_session_frames.txt      $(wc -l < "$ART/netns_router_session_frames.txt") lines"
echo "  $ART/netns_demo_host.txt"
echo "  $ART/netns_demo_client.txt"

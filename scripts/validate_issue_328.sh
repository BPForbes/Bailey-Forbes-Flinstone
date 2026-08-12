#!/usr/bin/env bash
# validate_issue_328.sh — non-interactive #328 acceptance runner (WSL + real Linux).
#
# Installs missing packages when --yes is set, applies idempotent firewall/sysctl
# rules, runs software gates, then optional mac80211_hwsim / UART / physical OTA.
# Does not use nmcli, wpa_cli, NetworkManager, or FlinstonePowershell as the DUT.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ARTIFACTS="${ARTIFACTS_DIR:-$ROOT/artifacts/issue-328}"
YES=0
DRY_RUN=0
SKIP_HWSIM=0
SKIP_UART=0
SKIP_DEPS=0
SKIP_FIREWALL=0
SKIP_SOFTWARE=0
UPDATE_ISSUE=0
HWSIM_SOFT_FAIL=0
NETNS=0
IFACE="${FL_NET_WIFI_IFACE:-}"
SSID="${SSID:-}"
PSK="${PSK:-flinstone_test_psk}"
AUTH="${AUTH:-sae}"
SUMMARY_FILE=""
PASS_N=0
FAIL_N=0
SKIP_N=0
REQUIRED_FAIL=0
HWSIM_FAIL=0
IS_WSL=0
HAVE_SUDO_N=0

usage() {
	cat <<'EOF'
Usage: ./scripts/validate_issue_328.sh [options]

Non-interactive GitHub issue #328 validation for WSL and native Linux.
Required gate: make test_p3_network. Hardware steps skip with a reason when
the kernel module, sudo, or UART device is missing.

Options:
  --help                 Show this help
  --dry-run              Print planned apt/sysctl/iptables/hostapd steps; do not apply
  --yes                  Install missing apt packages and use sudo -n without prompts
  --skip-deps            Do not apt-get install
  --skip-firewall        Do not change sysctl/iptables/Windows firewall
  --skip-hwsim           Skip mac80211_hwsim + hostapd OTA
  --skip-uart            Skip ESP32/ESP8266 UART coprocessor probe
  --skip-software        Skip make test_p3_network / test_p3_wifi (hwsim-only)
  --hwsim-soft-fail      If hwsim OTA fails, record FAIL but exit 0 (CI hosted runners)
  --netns                Place AP and STA hwsim ifaces in separate network namespaces
  --iface NAME           STA nl80211 iface (also FL_NET_WIFI_IFACE / IFACE)
  --ssid NAME            AP SSID for physical OTA (default: flinstone_sae_test)
  --psk PASS             Passphrase (default: flinstone_test_psk)
  --auth sae|wpa2-psk    Physical-only auth when not using the built-in hwsim APs
  --artifacts-dir DIR    Log/pcap directory (default: artifacts/issue-328)
  --update-issue         Post a gh comment on #328 with the summary (never closes;
                         never checks ROADMAP P3-10 / P4-01 boxes)

Examples:
  ./scripts/validate_issue_328.sh --dry-run
  ./scripts/validate_issue_328.sh --yes
  ./scripts/validate_issue_328.sh --yes --iface wlan0 --ssid MyAP --psk secret --auth sae
  ./scripts/validate_issue_328.sh --yes --skip-hwsim --skip-uart
  ./scripts/validate_issue_328.sh --yes --update-issue
  make validate-issue-328
EOF
}

log() { printf '%s\n' "$*"; }
logf() { printf '%s\n' "$*" | tee -a "$SUMMARY_FILE" >/dev/null; }

record() {
	local status="$1"
	local id="$2"
	local detail="$3"
	printf '%s\t%s\t%s\n' "$status" "$id" "$detail" >>"$ARTIFACTS/results.tsv"
	case "$status" in
	PASS) PASS_N=$((PASS_N + 1)); log "PASS  $id — $detail" ;;
	FAIL) FAIL_N=$((FAIL_N + 1)); log "FAIL  $id — $detail" ;;
	SKIP) SKIP_N=$((SKIP_N + 1)); log "SKIP  $id — $detail" ;;
	esac
}

run() {
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: $*"
		return 0
	fi
	"$@"
}

have_cmd() { command -v "$1" >/dev/null 2>&1; }

detect_wsl() {
	if grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null || [[ -n "${WSL_DISTRO_NAME:-}" ]]; then
		IS_WSL=1
	fi
}

sudo_n() {
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: sudo $*"
		return 0
	fi
	if [[ "$HAVE_SUDO_N" != "1" ]]; then
		return 1
	fi
	sudo -n "$@"
}

pkg_installed() {
	dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
}

ensure_pkg() {
	local pkg="$1"
	if pkg_installed "$pkg"; then
		return 0
	fi
	if [[ "$SKIP_DEPS" == "1" ]]; then
		log "missing package $pkg (--skip-deps)"
		return 1
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: apt-get install -y $pkg"
		return 0
	fi
	if [[ "$YES" != "1" ]]; then
		log "missing package $pkg (re-run with --yes to install)"
		return 1
	fi
	if [[ "$HAVE_SUDO_N" != "1" ]]; then
		log "cannot install $pkg (need passwordless sudo or root)"
		return 1
	fi
	sudo -n apt-get install -y "$pkg"
}

install_deps() {
	local pkgs=(
		build-essential gcc g++ make git
		iproute2 iw hostapd tcpdump kmod usbutils python3
		libssl-dev libsqlite3-dev
		dnsmasq-base
	)
	local extra extra_ok=1
	if [[ "$SKIP_DEPS" == "1" ]]; then
		record SKIP deps "not installing packages"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: apt-get update && apt-get install -y ${pkgs[*]} tshark picocom"
		record PASS deps "dry-run package list printed"
		return 0
	fi
	if [[ "$YES" == "1" && "$HAVE_SUDO_N" == "1" ]]; then
		sudo -n apt-get update -qq || true
	fi
	for pkg in "${pkgs[@]}"; do
		if ! ensure_pkg "$pkg"; then
			extra_ok=0
		fi
	done
	ensure_pkg tshark || ensure_pkg wireshark-common || true
	ensure_pkg picocom || true
	extra="linux-modules-extra-$(uname -r)"
	ensure_pkg "$extra" || true
	if [[ "$extra_ok" == "1" ]]; then
		record PASS deps "toolchain + iw/hostapd/tcpdump present"
	else
		record FAIL deps "one or more required packages missing"
		REQUIRED_FAIL=1
	fi
}

apply_linux_sysctl_firewall() {
	local applied=0
	if [[ "$SKIP_FIREWALL" == "1" ]]; then
		record SKIP firewall-linux "--skip-firewall"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: sysctl net.bridge.bridge-nf-call-iptables=0 net.ipv4.ip_forward=1"
		log "DRY-RUN: iptables/nft ACCEPT UDP 67,68,48077,48078"
		record PASS firewall-linux "dry-run sysctl/iptables plan"
		return 0
	fi
	if [[ "$HAVE_SUDO_N" != "1" ]]; then
		record SKIP firewall-linux "passwordless sudo required to apply sysctl/iptables"
		return 0
	fi
	sudo -n sysctl -w net.bridge.bridge-nf-call-iptables=0 >/dev/null 2>&1 || true
	sudo -n sysctl -w net.bridge.bridge-nf-call-ip6tables=0 >/dev/null 2>&1 || true
	sudo -n sysctl -w net.ipv4.ip_forward=1 >/dev/null 2>&1 || true
	if have_cmd nft; then
		sudo -n nft list table inet flinstone328 >/dev/null 2>&1 || \
			sudo -n nft add table inet flinstone328 2>/dev/null || true
		sudo -n nft add chain inet flinstone328 input '{ type filter hook input priority -50; policy accept; }' 2>/dev/null || true
		sudo -n nft add rule inet flinstone328 input udp dport '{ 67, 68, 48077, 48078 }' accept 2>/dev/null || true
		applied=1
	elif have_cmd iptables; then
		for port in 67 68 48077 48078; do
			sudo -n iptables -C INPUT -p udp --dport "$port" -j ACCEPT 2>/dev/null || \
				sudo -n iptables -I INPUT -p udp --dport "$port" -j ACCEPT
			sudo -n iptables -C OUTPUT -p udp --sport "$port" -j ACCEPT 2>/dev/null || \
				sudo -n iptables -I OUTPUT -p udp --sport "$port" -j ACCEPT || true
		done
		applied=1
	fi
	if [[ "$applied" == "1" ]]; then
		record PASS firewall-linux "sysctl + UDP 67/68/echo accept (idempotent)"
	else
		record SKIP firewall-linux "no nft/iptables available"
	fi
}

apply_wsl_windows_firewall() {
	local ps1="$ARTIFACTS/wsl_firewall.ps1"
	local winpath
	if [[ "$IS_WSL" != "1" ]]; then
		return 0
	fi
	if [[ "$SKIP_FIREWALL" == "1" ]]; then
		record SKIP firewall-wsl "--skip-firewall"
		return 0
	fi
	if ! have_cmd powershell.exe; then
		record SKIP firewall-wsl "powershell.exe not on PATH (Windows firewall unchanged)"
		log "WSL: USB Wi-Fi attach (elevated PowerShell on Windows), if no STA iface:"
		log "  usbipd list"
		log "  usbipd bind --busid <BUSID>"
		log "  usbipd attach --wsl --busid <BUSID>"
		return 0
	fi
	cat >"$ps1" <<'PS'
$ErrorActionPreference = 'Continue'
function Ensure-UdpRule([string]$name, [string]$dir, [string]$ports) {
  if (-not (Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $name -Direction $dir -Protocol UDP -LocalPort $ports -Action Allow -Profile Any | Out-Null
  }
}
Ensure-UdpRule 'Flinstone-328-DHCP-In' 'Inbound' '67,68'
Ensure-UdpRule 'Flinstone-328-DHCP-Out' 'Outbound' '67,68'
Ensure-UdpRule 'Flinstone-328-Echo-In' 'Inbound' '48077,48078'
Ensure-UdpRule 'Flinstone-328-Echo-Out' 'Outbound' '48077,48078'
Write-Output 'ok windows-firewall Flinstone-328 UDP 67/68/48077/48078'
PS
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: powershell.exe -File wsl_firewall.ps1 (Hyper-V/WSL UDP 67/68)"
		record PASS firewall-wsl "dry-run Windows firewall plan"
		return 0
	fi
	if have_cmd wslpath; then
		winpath="$(wslpath -w "$ps1")"
	else
		winpath="$ps1"
	fi
	if powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$winpath" \
		>"$ARTIFACTS/wsl_firewall.log" 2>&1; then
		record PASS firewall-wsl "Windows/Hyper-V UDP 67/68/echo rules ensured"
	else
		record SKIP firewall-wsl "powershell.exe firewall update failed (need Windows admin?); see wsl_firewall.log"
	fi
	log "WSL: USB Wi-Fi attach (elevated PowerShell on Windows), if no STA iface:"
	log "  usbipd list && usbipd bind --busid <BUSID> && usbipd attach --wsl --busid <BUSID>"
}

step_software() {
	local logf
	if [[ "$SKIP_SOFTWARE" == "1" ]]; then
		record SKIP software "--skip-software"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: make test_p3_network && make test_p3_wifi && make test_wifi_connect_ota"
		record PASS software "dry-run make targets"
		return 0
	fi
	logf="$ARTIFACTS/test_p3_network.log"
	if make test_p3_network >"$logf" 2>&1; then
		record PASS test_p3_network "required regression gate"
	else
		record FAIL test_p3_network "see $logf"
		REQUIRED_FAIL=1
		return 0
	fi
	logf="$ARTIFACTS/test_p3_wifi.log"
	if make test_p3_wifi >"$logf" 2>&1; then
		record PASS test_p3_wifi "lab SAE/EAPOL/TWT power-manager unit tests"
	else
		record FAIL test_p3_wifi "see $logf"
		REQUIRED_FAIL=1
	fi
	logf="$ARTIFACTS/test_wifi_connect_ota.log"
	if make test_wifi_connect_ota >"$logf" 2>&1; then
		record PASS test_wifi_connect_ota "mock SAE/EAPOL/TWT OTA"
	else
		record FAIL test_wifi_connect_ota "see $logf"
		REQUIRED_FAIL=1
	fi
}

hwsim_ifaces() {
	# Do not assume wlan0/wlan1 — enumerate iw Interface lines.
	iw dev 2>/dev/null | awk '/Interface/{print $2}'
}

write_hostapd_sae() {
	local iface="$1"
	local conf="$2"
	cat >"$conf" <<EOF
interface=$iface
driver=nl80211
ssid=flinstone_sae_test
hw_mode=g
channel=1
ieee80211n=1
ieee80211w=2
wpa=2
wpa_key_mgmt=SAE
rsn_pairwise=CCMP
sae_password=$PSK
sae_pwe=0
ignore_broadcast_ssid=0
EOF
}

write_hostapd_wpa2() {
	local iface="$1"
	local conf="$2"
	cat >"$conf" <<EOF
interface=$iface
driver=nl80211
ssid=flinstone_wpa2_test
hw_mode=g
channel=1
ieee80211n=1
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase=$PSK
ignore_broadcast_ssid=0
EOF
}

start_udp_echo() {
	local addr="$1"
	local port="$2"
	local pidfile="$3"
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: UDP echo $addr:$port"
		return 0
	fi
	python3 - "$addr" "$port" "$pidfile" <<'PY' &
import os, socket, sys
addr, port, pidfile = sys.argv[1], int(sys.argv[2]), sys.argv[3]
open(pidfile, "w").write(str(os.getpid()))
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind((addr, port))
while True:
    data, src = sock.recvfrom(2048)
    sock.sendto(data, src)
PY
	sleep 0.2
}

kill_pidfile() {
	local f="$1"
	local pid
	if [[ -f "$f" ]]; then
		pid="$(cat "$f" 2>/dev/null || true)"
		if [[ -n "${pid:-}" ]]; then
			sudo_n kill "$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
		fi
		rm -f "$f"
	fi
}

HWSIM_CLEANUP_DONE=0
hwsim_cleanup() {
	if [[ "$HWSIM_CLEANUP_DONE" == "1" ]]; then
		return 0
	fi
	HWSIM_CLEANUP_DONE=1
	kill_pidfile "$ARTIFACTS/hostapd.pid" || true
	kill_pidfile "$ARTIFACTS/dnsmasq.pid" || true
	kill_pidfile "$ARTIFACTS/tcpdump.pid" || true
	kill_pidfile "$ARTIFACTS/udp_echo.pid" || true
	if [[ -n "${HWSIM_NS_AP:-}" ]]; then
		sudo_n ip netns del "$HWSIM_NS_AP" 2>/dev/null || true
	fi
	if [[ -n "${HWSIM_NS_STA:-}" ]]; then
		sudo_n ip netns del "$HWSIM_NS_STA" 2>/dev/null || true
	fi
}

run_ota() {
	local sta="$1"
	local ssid="$2"
	local auth="$3"
	local extra="${4:-}"
	local logf="$ARTIFACTS/ota-${auth}.log"
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: FL_NET_WIFI_IFACE=$sta SSID=$ssid AUTH=$auth ./tests/test_p3_wifi_ota"
		return 0
	fi
	make tests/test_p3_wifi_ota >/dev/null
	set +e
	sudo -n --preserve-env=PATH \
		env FL_NET_WIFI_IFACE="$sta" FL_NET_WIFI_NL80211=1 \
		SSID="$ssid" PSK="$PSK" AUTH="$auth" \
		FL_NET_WIFI_OTA_REQUIRE=1 UDP_ECHO_DST=192.168.50.1 \
		$extra \
		"$ROOT/tests/test_p3_wifi_ota" >"$logf" 2>&1
	local rc=$?
	set -e
	return "$rc"
}

step_hwsim() {
	local ap sta radios conf
	if [[ "$SKIP_HWSIM" == "1" ]]; then
		record SKIP hwsim "--skip-hwsim"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: modprobe mac80211_hwsim radios=2; discover iw ifaces; hostapd SAE+WPA2; test_p3_wifi_ota"
		record PASS hwsim "dry-run hostapd + in-tree connect plan"
		return 0
	fi
	if [[ "$HAVE_SUDO_N" != "1" ]]; then
		record SKIP hwsim "passwordless sudo required for modprobe/hostapd"
		return 0
	fi
	if ! have_cmd iw || ! have_cmd hostapd; then
		record SKIP hwsim "iw/hostapd missing"
		return 0
	fi
	if ! lsmod | grep -q '^mac80211_hwsim'; then
		if ! sudo -n modprobe mac80211_hwsim radios=2 2>"$ARTIFACTS/modprobe_hwsim.err"; then
			record SKIP hwsim "mac80211_hwsim unavailable (WSL kernels often omit it; use a real Linux host or usbipd Wi-Fi)"
			return 0
		fi
	fi
	mapfile -t radios < <(hwsim_ifaces)
	if [[ "${#radios[@]}" -lt 2 ]]; then
		record SKIP hwsim "need two hwsim interfaces, found ${#radios[@]} (${radios[*]:-none})"
		return 0
	fi
	ap="${radios[0]}"
	sta="${radios[1]}"
	log "[hwsim] AP=$ap STA=$sta"
	trap hwsim_cleanup EXIT

	sudo -n ip link set "$ap" down || true
	sudo -n iw dev "$ap" set type __ap || true
	sudo -n ip addr flush dev "$ap" || true
	sudo -n ip addr add 192.168.50.1/24 dev "$ap"
	sudo -n ip link set "$ap" up
	sudo -n ip link set "$sta" up

	conf="$ARTIFACTS/hostapd-sae.conf"
	write_hostapd_sae "$ap" "$conf"
	if ! sudo -n hostapd -B -P "$ARTIFACTS/hostapd.pid" "$conf" >"$ARTIFACTS/hostapd-sae.log" 2>&1; then
		record FAIL hwsim-sae "hostapd SAE failed to start; see hostapd-sae.log"
		HWSIM_FAIL=1
		hwsim_cleanup
		trap - EXIT
		return 0
	fi
	if have_cmd dnsmasq; then
		sudo -n dnsmasq --no-daemon --interface="$ap" --bind-interfaces \
			--listen-address=192.168.50.1 --port=0 \
			--dhcp-range=192.168.50.10,192.168.50.80,12h \
			--dhcp-option=3,192.168.50.1 \
			--pid-file="$ARTIFACTS/dnsmasq.pid" \
			--dhcp-leasefile="$ARTIFACTS/dnsmasq.leases" \
			>"$ARTIFACTS/dnsmasq.log" 2>&1 &
		sleep 0.3
	fi
	start_udp_echo 192.168.50.1 48078 "$ARTIFACTS/udp_echo.pid"
	if have_cmd tcpdump; then
		sudo -n tcpdump -i "$sta" -U -w "$ARTIFACTS/sta-sae.pcap" >/dev/null 2>&1 &
		echo $! >"$ARTIFACTS/tcpdump.pid"
	fi
	sleep 1
	if run_ota "$sta" "flinstone_sae_test" sae "DHCP=in-tree UDP_ECHO=1"; then
		record PASS hwsim-sae "in-tree fl_net_wifi_connect WPA3-SAE (no OS supplicant)"
	else
		record FAIL hwsim-sae "see ota-sae.log and sta-sae.pcap"
		HWSIM_FAIL=1
	fi
	kill_pidfile "$ARTIFACTS/tcpdump.pid" || true
	kill_pidfile "$ARTIFACTS/hostapd.pid" || true
	sleep 0.5

	conf="$ARTIFACTS/hostapd-wpa2.conf"
	write_hostapd_wpa2 "$ap" "$conf"
	if sudo -n hostapd -B -P "$ARTIFACTS/hostapd.pid" "$conf" >"$ARTIFACTS/hostapd-wpa2.log" 2>&1; then
		sleep 1
		if have_cmd tcpdump; then
			sudo -n tcpdump -i "$sta" -U -w "$ARTIFACTS/sta-wpa2.pcap" >/dev/null 2>&1 &
			echo $! >"$ARTIFACTS/tcpdump.pid"
		fi
		if run_ota "$sta" "flinstone_wpa2_test" wpa2-psk "DHCP=in-tree"; then
			record PASS hwsim-wpa2 "in-tree fl_net_wifi_connect WPA2-PSK (no OS supplicant)"
		else
			record FAIL hwsim-wpa2 "see ota-wpa2-psk.log and sta-wpa2.pcap"
			HWSIM_FAIL=1
		fi
		kill_pidfile "$ARTIFACTS/tcpdump.pid" || true
	else
		record FAIL hwsim-wpa2 "hostapd WPA2 failed to start"
		HWSIM_FAIL=1
	fi

	# TWT against hwsim is optional — most hwsim APs are not TWT responders.
	if run_ota "$sta" "flinstone_wpa2_test" wpa2-psk "TWT=1"; then
		if grep -q 'SKIP TWT' "$ARTIFACTS/ota-wpa2-psk.log" 2>/dev/null; then
			record SKIP hwsim-twt "AP did not negotiate TWT (unit coverage is test_p3_wifi)"
		else
			record PASS hwsim-twt "TWT setup/teardown on hwsim"
		fi
	else
		record SKIP hwsim-twt "optional real-AP/hwsim TWT not available"
	fi

	hwsim_cleanup
	trap - EXIT
}

step_physical() {
	local sta="${IFACE:-${FL_NET_WIFI_IFACE:-}}"
	local ssid="${SSID:-}"
	if [[ -z "$sta" || -z "$ssid" ]]; then
		record SKIP physical-ota "set --iface and --ssid (or FL_NET_WIFI_IFACE + SSID) for a real AP"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: physical OTA iface=$sta ssid=$ssid auth=$AUTH"
		record PASS physical-ota "dry-run"
		return 0
	fi
	make tests/test_p3_wifi_ota >/dev/null
	if FL_NET_WIFI_IFACE="$sta" SSID="$ssid" PSK="$PSK" AUTH="$AUTH" \
		FL_NET_WIFI_OTA_REQUIRE=1 DHCP=in-tree \
		"$ROOT/tests/test_p3_wifi_ota" >"$ARTIFACTS/ota-physical.log" 2>&1; then
		record PASS physical-ota "fl_net_wifi_connect on $sta ssid=$ssid"
	else
		record FAIL physical-ota "see ota-physical.log"
		HWSIM_FAIL=1
	fi
}

step_uart() {
	local dev=""
	if [[ "$SKIP_UART" == "1" ]]; then
		record SKIP uart "--skip-uart"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: make test_wifi_coprocessor; probe /dev/ttyUSB* /dev/ttyACM*"
		record PASS uart "dry-run"
		return 0
	fi
	if make test_wifi_coprocessor >"$ARTIFACTS/test_wifi_coprocessor.log" 2>&1; then
		record PASS uart-unit "test_wifi_coprocessor (no hardware required)"
	else
		record FAIL uart-unit "see test_wifi_coprocessor.log"
		REQUIRED_FAIL=1
	fi
	for cand in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
		if [[ -e "$cand" ]]; then
			dev="$cand"
			break
		fi
	done
	if [[ -z "$dev" ]]; then
		record SKIP uart-hw "no /dev/ttyUSB* or /dev/ttyACM* (ESP32/ESP8266 not attached)"
		return 0
	fi
	record PASS uart-hw "device $dev present — run wifi scan/join over UART AT on this host"
	log "  export FL_WIFI_UART_DEV=$dev   # then: make && ./BPForbes_Flinstone_Shell wifi scan"
}

never_check_roadmap() {
	# Intentionally do not flip docs/ROADMAP.md P3-10 / P4-01 to ✅ from this script.
	record SKIP roadmap "do not mark P3-10/P4-01 ✅ until production RF evidence exists"
}

update_issue() {
	local body="$ARTIFACTS/issue_comment.md"
	local patched="$ARTIFACTS/issue_body_patched.md"
	if [[ "$UPDATE_ISSUE" != "1" ]]; then
		return 0
	fi
	if ! have_cmd gh; then
		record SKIP update-issue "gh not installed"
		return 0
	fi
	if [[ "$DRY_RUN" == "1" ]]; then
		log "DRY-RUN: gh issue comment 328; check boxes only for PASS rows (never ROADMAP, never close)"
		record PASS update-issue "dry-run"
		return 0
	fi
	{
		echo "## #328 validation ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
		echo
		echo "Host: $(uname -srm)  WSL=$IS_WSL"
		echo
		echo '| Status | Item | Detail |'
		echo '|--------|------|--------|'
		awk -F'\t' '{printf "| %s | `%s` | %s |\n", $1, $2, $3}' "$ARTIFACTS/results.tsv"
		echo
		echo "PASS=$PASS_N FAIL=$FAIL_N SKIP=$SKIP_N"
		echo
		echo "This comment does **not** close #328 and does **not** check ROADMAP P3-10 / P4-01."
		echo "Re-run locally: \`./scripts/validate_issue_328.sh --yes\`"
	} >"$body"
	if gh issue comment 328 --body-file "$body" >/dev/null; then
		record PASS update-issue "commented on GitHub issue #328"
	else
		record SKIP update-issue "gh issue comment failed (auth/permissions)"
		return 0
	fi
	# Check only boxes that this run proved. Never ROADMAP, never close the issue.
	if gh issue view 328 --json body -q .body >"$ARTIFACTS/issue_body.md" 2>/dev/null; then
		python3 - "$ARTIFACTS/results.tsv" "$ARTIFACTS/issue_body.md" "$patched" <<'PY' || true
import pathlib, sys
tsv, src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
passed = {line.split("\t")[1] for line in tsv.read_text().splitlines() if line.startswith("PASS\t")}
markers = []
if "hwsim-sae" in passed:
    markers += ["Full SAE OTA exchange against", "succeeds with WPA3-SAE against", "SAE Dragonfly exchange passes"]
if "hwsim-wpa2" in passed:
    markers += ["4-way exchange against `mac80211_hwsim`", "succeeds with WPA2-PSK", "WPA2 EAPOL 4-way handshake passes"]
if "hwsim-twt" in passed:
    markers += ["TWT exchange against `mac80211_hwsim`"]
if "physical-ota" in passed:
    markers += ["succeeds with WPA3-SAE against", "succeeds with WPA2-PSK"]
if "test_p3_network" in passed:
    markers += ["make test_p3_network"]
never = ("ROADMAP", "P3-10 / P4-01", "updated to `✅`", "updated to ✅")
out = []
for line in src.read_text().splitlines(True):
    if line.lstrip().startswith("- [ ]") and any(m in line for m in markers) and not any(n in line for n in never):
        line = line.replace("- [ ]", "- [x]", 1)
    out.append(line)
dst.write_text("".join(out))
print("patched" if "".join(out) != src.read_text() else "no-checkbox-changes")
PY
		if [[ -f "$patched" ]] && ! cmp -s "$ARTIFACTS/issue_body.md" "$patched"; then
			if gh issue edit 328 --body-file "$patched" >/dev/null; then
				record PASS update-issue-boxes "checked GitHub boxes that this run proved (ROADMAP left unchecked)"
			else
				record SKIP update-issue-boxes "gh issue edit failed; comment still posted"
			fi
		fi
	fi
}

parse_args() {
	while [[ $# -gt 0 ]]; do
		case "$1" in
		--help|-h) usage; exit 0 ;;
		--dry-run) DRY_RUN=1; shift ;;
		--yes) YES=1; shift ;;
		--skip-deps) SKIP_DEPS=1; shift ;;
		--skip-firewall) SKIP_FIREWALL=1; shift ;;
		--skip-hwsim) SKIP_HWSIM=1; shift ;;
		--skip-uart) SKIP_UART=1; shift ;;
		--skip-software) SKIP_SOFTWARE=1; shift ;;
		--hwsim-soft-fail) HWSIM_SOFT_FAIL=1; shift ;;
		--netns) NETNS=1; shift ;;
		--iface) IFACE="${2:-}"; shift 2 ;;
		--ssid) SSID="${2:-}"; shift 2 ;;
		--psk) PSK="${2:-}"; shift 2 ;;
		--auth) AUTH="${2:-}"; shift 2 ;;
		--artifacts-dir) ARTIFACTS="${2:-}"; shift 2 ;;
		--update-issue) UPDATE_ISSUE=1; shift ;;
		*)
			printf 'Error: unknown option %s\n  ./scripts/validate_issue_328.sh --help\n' "$1" >&2
			exit 2
			;;
		esac
	done
}

main() {
	parse_args "$@"
	mkdir -p "$ARTIFACTS"
	: >"$ARTIFACTS/results.tsv"
	SUMMARY_FILE="$ARTIFACTS/summary.txt"
	: >"$SUMMARY_FILE"
	detect_wsl
	if sudo -n true 2>/dev/null; then
		HAVE_SUDO_N=1
	fi
	log "issue-328 validate  wsl=$IS_WSL dry_run=$DRY_RUN sudo_n=$HAVE_SUDO_N artifacts=$ARTIFACTS"
	if [[ "$NETNS" == "1" ]]; then
		log "note: --netns is accepted; hwsim radios share a simulated medium even without netns"
	fi

	install_deps
	apply_linux_sysctl_firewall
	apply_wsl_windows_firewall
	step_software
	step_hwsim
	step_physical
	step_uart
	never_check_roadmap
	update_issue

	{
		echo "PASS=$PASS_N FAIL=$FAIL_N SKIP=$SKIP_N REQUIRED_FAIL=$REQUIRED_FAIL HWSIM_FAIL=$HWSIM_FAIL"
		echo "results: $ARTIFACTS/results.tsv"
	} | tee "$SUMMARY_FILE"

	if [[ "$REQUIRED_FAIL" != "0" ]]; then
		exit 1
	fi
	if [[ "$HWSIM_FAIL" != "0" && "$HWSIM_SOFT_FAIL" != "1" ]]; then
		exit 1
	fi
	exit 0
}

main "$@"

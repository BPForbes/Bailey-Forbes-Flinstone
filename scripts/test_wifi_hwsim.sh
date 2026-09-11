#!/usr/bin/env bash
# Optional mac80211_hwsim OTA validation for #328.
# Gated by FL_NET_WIFI_HWSIM_OK=1 (requires modprobe mac80211_hwsim, iw, ip).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ "${FL_NET_WIFI_HWSIM_OK:-0}" != "1" ]]; then
	echo "test_wifi_hwsim: skipped (set FL_NET_WIFI_HWSIM_OK=1 to run)"
	echo "  needs: sudo, mac80211_hwsim kernel module, iw, iproute2"
	exit 0
fi

if ! sudo -n true 2>/dev/null; then
	echo "test_wifi_hwsim: skipped (passwordless sudo required)" >&2
	exit 0
fi

if ! lsmod | awk '$1 == "mac80211_hwsim" { found=1 } END { exit !found }'; then
	if ! sudo modprobe mac80211_hwsim radios=2 2>/dev/null; then
		echo "test_wifi_hwsim: skipped (mac80211_hwsim module unavailable)" >&2
		exit 0
	fi
fi

WLAN_AP="$(iw dev 2>/dev/null | awk '/Interface/{print $2; exit}')"
WLAN_STA="$(iw dev 2>/dev/null | awk '/Interface/{ifaces[++n]=$2} END{if(n>=2) print ifaces[2]; else if(n==1) print ifaces[1]}')"
if [[ -z "${WLAN_STA:-}" ]]; then
	echo "test_wifi_hwsim: skipped (no hwsim interfaces visible)" >&2
	exit 0
fi

echo "[hwsim] AP iface=${WLAN_AP:-none} STA iface=${WLAN_STA}"
echo "[hwsim] running #328 hostapd SAE/WPA2 playbook (validate_issue_328 step_hwsim)"
exec "$ROOT/scripts/validate_issue_328.sh" --yes --skip-uart --skip-software "$@"

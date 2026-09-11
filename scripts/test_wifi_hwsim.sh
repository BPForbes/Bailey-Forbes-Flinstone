#!/usr/bin/env bash
# #328 item 1 — mac80211_hwsim virtual AP + station CI environment.
# Loads two hwsim radios, runs the in-tree OTA suite (SAE, WPA2-PSK/EAPOL,
# optional TWT, DHCP, UDP echo), and keeps logs/pcaps under artifacts/.
#
# Local opt-in:  FL_NET_WIFI_HWSIM_OK=1 make test_wifi_hwsim
# CI:            CI=true make test_wifi_hwsim
# Require OTA:   FL_NET_WIFI_HWSIM_REQUIRE=1 (fail if the module cannot load)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ARTIFACTS="${ARTIFACTS_DIR:-$ROOT/artifacts/issue-328-hwsim}"
mkdir -p "$ARTIFACTS"

in_ci=0
if [[ "${CI:-}" == "true" || -n "${GITHUB_ACTIONS:-}" ]]; then
	in_ci=1
fi

if [[ "${FL_NET_WIFI_HWSIM_OK:-0}" != "1" && "$in_ci" != "1" ]]; then
	echo "test_wifi_hwsim: skipped (set FL_NET_WIFI_HWSIM_OK=1 or CI=true to run)"
	echo "  needs: sudo, mac80211_hwsim kernel module, iw, hostapd, iproute2"
	echo "skipped" >"$ARTIFACTS/hwsim-status.txt"
	exit 0
fi

if ! command -v sudo >/dev/null 2>&1 || ! sudo -n true 2>/dev/null; then
	echo "test_wifi_hwsim: skipped (passwordless sudo required)" | tee "$ARTIFACTS/hwsim-status.txt"
	if [[ "${FL_NET_WIFI_HWSIM_REQUIRE:-0}" == "1" ]]; then
		exit 1
	fi
	exit 0
fi

if ! command -v iw >/dev/null 2>&1 || ! command -v hostapd >/dev/null 2>&1; then
	echo "test_wifi_hwsim: skipped (iw/hostapd missing)" | tee "$ARTIFACTS/hwsim-status.txt"
	if [[ "${FL_NET_WIFI_HWSIM_REQUIRE:-0}" == "1" ]]; then
		exit 1
	fi
	exit 0
fi

if ! lsmod | awk '$1 == "mac80211_hwsim" { found=1 } END { exit !found }'; then
	if ! sudo -n modprobe mac80211_hwsim radios=2 2>"$ARTIFACTS/modprobe_hwsim.err"; then
		echo "test_wifi_hwsim: skipped (mac80211_hwsim module unavailable)" | tee "$ARTIFACTS/hwsim-status.txt"
		cat "$ARTIFACTS/modprobe_hwsim.err" >>"$ARTIFACTS/hwsim-status.txt" || true
		if [[ "${FL_NET_WIFI_HWSIM_REQUIRE:-0}" == "1" ]]; then
			exit 1
		fi
		exit 0
	fi
fi

echo "loaded" >"$ARTIFACTS/hwsim-status.txt"
echo "[hwsim] mac80211_hwsim present — running #328 OTA suite (logs in $ARTIFACTS)"
# Soft-fail only when the caller asked; CI runs the suite for real once the module is up.
extra=()
if [[ "${FL_NET_WIFI_HWSIM_SOFT_FAIL:-0}" == "1" ]]; then
	extra+=(--hwsim-soft-fail)
fi
exec "$ROOT/scripts/validate_issue_328.sh" --yes --skip-uart --skip-software \
	--artifacts-dir "$ARTIFACTS" "${extra[@]}" "$@"

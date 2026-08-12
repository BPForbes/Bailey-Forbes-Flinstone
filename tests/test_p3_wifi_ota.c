/**
 * #328 — physical / mac80211_hwsim OTA: fl_net_wifi_connect on nl80211 FullMAC.
 *
 * Env (all optional except SSID+IFACE to actually run):
 *   FL_NET_WIFI_IFACE or IFACE
 *   SSID or FL_NET_WIFI_SSID
 *   PSK / PASSPHRASE / FL_NET_WIFI_PSK
 *   AUTH=sae|wpa3-sae|wpa2-psk|wpa2 (default sae)
 *   TWT=1
 *   UDP_ECHO=1
 *   UDP_ECHO_DST=a.b.c.d (default 192.168.50.1)
 *   FL_NET_WIFI_OTA_REQUIRE=1  — fail instead of skip when iface/SSID missing
 *
 * Does not call nmcli, wpa_cli, NetworkManager, or FlinstonePowershell.
 */
#include "net_wifi_station.h"
#include "net_iface.h"
#include "net_route.h"
#include "net_udp.h"
#include "net_ipv4.h"
#include "net_wifi_twt.h"
#include "contract_p3_wifi.h"
#include "contract_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int env_truthy(const char *name)
{
	const char *v = getenv(name);

	return v && v[0] && strcmp(v, "0") != 0;
}

static const char *env_or(const char *a, const char *b)
{
	const char *v = getenv(a);

	if (v && v[0])
		return v;
	v = getenv(b);
	if (v && v[0])
		return v;
	return NULL;
}

static uint8_t parse_auth(const char *s)
{
	if (!s || !s[0])
		return FL_WIFI_AUTH_WPA3_SAE;
	if (strcmp(s, "wpa2") == 0 || strcmp(s, "wpa2-psk") == 0 || strcmp(s, "psk") == 0)
		return FL_WIFI_AUTH_WPA2_PSK;
	if (strcmp(s, "open") == 0)
		return FL_WIFI_AUTH_OPEN;
	return FL_WIFI_AUTH_WPA3_SAE;
}

static int skip_or_fail(const char *why)
{
	if (env_truthy("FL_NET_WIFI_OTA_REQUIRE")) {
		fprintf(stderr, "FAIL test_p3_wifi_ota: %s\n", why);
		return 1;
	}
	printf("SKIP test_p3_wifi_ota: %s\n", why);
	return 0;
}

int main(void)
{
	const char *iface;
	const char *ssid;
	const char *psk;
	fl_net_wifi_cred_t cred;
	fl_result_t rc;
	unsigned timeout_ms = 20000u;

	iface = env_or("FL_NET_WIFI_IFACE", "IFACE");
	ssid = env_or("SSID", "FL_NET_WIFI_SSID");
	psk = env_or("PSK", "PASSPHRASE");
	if (!psk)
		psk = getenv("FL_NET_WIFI_PSK");

	if (!iface || !iface[0])
		return skip_or_fail("no FL_NET_WIFI_IFACE/IFACE (nl80211 STA)");
	if (!ssid || !ssid[0])
		return skip_or_fail("no SSID/FL_NET_WIFI_SSID");

	(void)unsetenv("FL_NET_WIFI_LAB");
	(void)unsetenv("FL_NET_WIFI_FULLMAC_LAB");
	(void)unsetenv("FL_WIFI_80211AX_MOCK");
	(void)unsetenv("FL_WIFI_UART_FD");
	(void)unsetenv("FL_NET_WIFI_USE_WPA");
	(void)unsetenv("FL_NET_WIFI_FLINSTONE_PS");
	(void)unsetenv("FL_NET_WIFI_FLINSTONE_LINUX");
	if (setenv("FL_NET_WIFI_NL80211", "1", 1) != 0)
		return 1;
	if (setenv("FL_NET_WIFI_IFACE", iface, 1) != 0)
		return 1;

	memset(&cred, 0, sizeof(cred));
	strncpy(cred.ssid, ssid, sizeof(cred.ssid) - 1u);
	if (psk)
		strncpy(cred.passphrase, psk, sizeof(cred.passphrase) - 1u);
	cred.auth_mode = parse_auth(getenv("AUTH"));

	fl_net_route_init();
	if (fl_net_wifi_station_init() != FL_RESULT_OK) {
		fprintf(stderr, "FAIL test_p3_wifi_ota: station_init\n");
		return 1;
	}

	printf("test_p3_wifi_ota: iface=%s ssid=%s auth=%u\n", iface, ssid,
	       (unsigned)cred.auth_mode);

	rc = fl_net_wifi_scan(FL_WIFI_BAND_ANY, 8000u);
	if (rc != FL_RESULT_OK)
		fprintf(stderr, "warn test_p3_wifi_ota: scan rc=%d (continuing)\n", (int)rc);

	rc = fl_net_wifi_connect(&cred, timeout_ms);
	fl_net_wifi_cred_scrub_passphrase(&cred);
	if (rc != FL_RESULT_OK) {
		fprintf(stderr, "FAIL test_p3_wifi_ota: fl_net_wifi_connect rc=%d\n", (int)rc);
		return 1;
	}
	if (fl_net_wifi_station_host_backend()) {
		fprintf(stderr, "FAIL test_p3_wifi_ota: used hosted OS supplicant (nmcli/wpa_cli)\n");
		(void)fl_net_wifi_disconnect();
		return 1;
	}
	if (!fl_net_wifi_station_physical_backend()) {
		fprintf(stderr, "FAIL test_p3_wifi_ota: not physical nl80211 FullMAC path\n");
		(void)fl_net_wifi_disconnect();
		return 1;
	}
	if (fl_net_wifi_state() != FL_WIFI_STATE_UP) {
		fprintf(stderr, "FAIL test_p3_wifi_ota: state not UP\n");
		(void)fl_net_wifi_disconnect();
		return 1;
	}
	printf("ok #328 fl_net_wifi_connect physical nl80211 (no OS supplicant)\n");

	if (env_truthy("TWT")) {
		fl_net_wifi_twt_params_t req = {.wake_duration_us = 8000u,
						.wake_interval_us = 100000u,
						.implicit = 1};
		fl_net_wifi_twt_params_t agreed = {0};

		if (fl_net_wifi_twt_setup(&req, &agreed) != FL_RESULT_OK) {
			printf("SKIP TWT: AP did not negotiate a flow_id\n");
		} else {
			printf("ok #328 TWT flow_id=%u next_wake_us=%llu\n",
			       (unsigned)agreed.flow_id,
			       (unsigned long long)fl_net_wifi_twt_next_wake_us());
			(void)fl_net_wifi_twt_teardown(agreed.flow_id);
			if (fl_net_wifi_twt_next_wake_us() != 0u) {
				fprintf(stderr, "FAIL test_p3_wifi_ota: TWT teardown left a schedule\n");
				(void)fl_net_wifi_disconnect();
				return 1;
			}
		}
	}

	if (env_truthy("UDP_ECHO")) {
		const char *dst = getenv("UDP_ECHO_DST");
		uint32_t gw_be = 0u;
		const char payload[] = "issue-328-udp-echo";
		uint8_t rx[128];
		size_t rx_len = 0;

		if (!dst || !dst[0])
			dst = "192.168.50.1";
		if (!fl_net_ipv4_parse_literal(dst, &gw_be) || gw_be == 0u) {
			fprintf(stderr, "FAIL test_p3_wifi_ota: bad UDP_ECHO_DST=%s\n", dst);
			(void)fl_net_wifi_disconnect();
			return 1;
		}
		fl_net_udp_demux_reset();
		if (fl_net_udp_bind_port(48077u) != FL_RESULT_OK) {
			fprintf(stderr, "FAIL test_p3_wifi_ota: udp bind\n");
			(void)fl_net_wifi_disconnect();
			return 1;
		}
		rc = fl_net_udp_echo_exchange(gw_be, 48077u, 48078u, (const uint8_t *)payload,
					      strlen(payload), rx, sizeof(rx), &rx_len, 4000u);
		if (rc != FL_RESULT_OK) {
			fprintf(stderr, "FAIL test_p3_wifi_ota: UDP echo rc=%d dst=%s\n", (int)rc,
				dst);
			(void)fl_net_wifi_disconnect();
			return 1;
		}
		printf("ok #328 in-tree UDP echo via Wi-Fi fl_net_driver_t\n");
	}

	(void)fl_net_wifi_disconnect();
	printf("test_p3_wifi_ota: passed\n");
	return 0;
}

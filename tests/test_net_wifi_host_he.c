/*
 * Unit tests for host-side 802.11ax scan HE hints (no iw subprocess required).
 */

#include "net_wifi_he.h"

#include <stdio.h>
#include <string.h>

static int tests_run;
static int tests_failed;

static void expect_true(int cond, const char *name)
{
	tests_run++;
	if (!cond) {
		tests_failed++;
		fprintf(stderr, "FAIL: %s\n", name);
	}
}

static void test_he_hint_6ghz(void)
{
	fl_net_wifi_scan_entry_t e;

	memset(&e, 0, sizeof(e));
	e.band = FL_WIFI_BAND_6GHZ;
	fl_net_wifi_host_he_hint(&e, NULL, NULL);
	expect_true(e.he_supported == 1u, "6 GHz implies HE");
	expect_true(e.channel_width_mhz >= 80u, "6 GHz default width");
}

static void test_he_hint_nmcli_rate(void)
{
	fl_net_wifi_scan_entry_t e;

	memset(&e, 0, sizeof(e));
	e.band = FL_WIFI_BAND_5GHZ;
	fl_net_wifi_host_he_hint(&e, "WPA2", "540 Mbit/s");
	expect_true(e.he_supported == 1u, "540 Mbit/s implies HE");
}

static void test_he_cap_from_entry(void)
{
	fl_net_wifi_scan_entry_t ap;
	fl_net_wifi_he_cap_t cap;

	memset(&ap, 0, sizeof(ap));
	ap.he_supported = 1u;
	ap.bss_color = 7u;
	ap.channel_width_mhz = 80u;
	ap.twt_responder = 1u;
	fl_net_wifi_host_he_cap_from_entry(&ap, &cap);
	expect_true(cap.supports_ofdma == 1u, "HE cap OFDMA");
	expect_true(cap.bss_color == 7u, "HE cap bss color");
	expect_true(cap.supports_twt == 1u, "HE cap TWT");
}

int main(void)
{
	test_he_hint_6ghz();
	test_he_hint_nmcli_rate();
	test_he_cap_from_entry();
	if (tests_failed) {
		fprintf(stderr, "test_net_wifi_host_he: %d/%d failed\n",
			tests_failed, tests_run);
		return 1;
	}
	printf("test_net_wifi_host_he: all %d passed\n", tests_run);
	return 0;
}

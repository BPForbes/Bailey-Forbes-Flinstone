/*
 * Unit tests for Phase 4 FullMAC PCI probe helpers (no hardware required).
 */

#include "wifi_fullmac_chipset.h"

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

static void test_chipset_table(void)
{
	size_t n = 0;
	const wifi_fullmac_chipset_t *t = wifi_fullmac_chipset_table(&n);

	expect_true(n >= 8u, "chipset table has ax entries");
	expect_true(t != NULL && t[0].vendor_id != 0u, "first chipset row valid");
	expect_true(wifi_fullmac_chipset_match(0x8086u, 0x2725u) != NULL,
		    "Intel AX210 match");
	expect_true(wifi_fullmac_chipset_match(0x14c3u, 0x7961u) != NULL,
		    "MT7921 match");
	expect_true(wifi_fullmac_chipset_match(0xffffu, 0x0000u) == NULL,
		    "unknown vid:did rejected");
}

int main(void)
{
	test_chipset_table();
	if (tests_failed) {
		fprintf(stderr, "test_wifi_fullmac_probe: %d/%d failed\n",
			tests_failed, tests_run);
		return 1;
	}
	printf("test_wifi_fullmac_probe: all %d passed\n", tests_run);
	return 0;
}

/*
 * Phase 4 PCIe FullMAC: sysfs/PCI config probe, BAR decode.
 * Chipset-specific firmware/command rings land in follow-up drivers per VID:PID.
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_chipset.h"
#include "wifi_fullmac_hw_internal.h"

#include "fl/driver/pci.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#endif

static int pcie_parse_bdf(const char *s, uint8_t *bus, uint8_t *dev, uint8_t *fn)
{
	unsigned dom = 0, b = 0, d = 0, f = 0;

	if (!s || !bus || !dev || !fn)
		return -1;
	if (sscanf(s, "%x:%x:%x.%x", &dom, &b, &d, &f) == 4) {
		(void)dom;
	} else if (sscanf(s, "%x:%x.%x", &b, &d, &f) != 3)
		return -1;
	if (b > 255u || d > 31u || f > 7u)
		return -1;
	*bus = (uint8_t)b;
	*dev = (uint8_t)d;
	*fn = (uint8_t)f;
	return 0;
}

static uint32_t pcie_decode_bar0(uint32_t bar0_raw)
{
	if (bar0_raw == 0u || bar0_raw == 0xffffffffu)
		return 0u;
	if ((bar0_raw & 0x1u) == 0u)
		return bar0_raw & ~0xFu;
	return 0u;
}

static int pcie_read_ids(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t *vid, uint16_t *did)
{
	uint32_t id;

	if (!vid || !did)
		return -1;
	id = pci_read_config32(bus, dev, fn, 0u);
	if (id == 0xffffffffu || id == 0u)
		return -1;
	*vid = (uint16_t)(id & 0xffffu);
	*did = (uint16_t)((id >> 16) & 0xffffu);
	return 0;
}

static int pcie_apply_chipset(uint16_t vid, uint16_t did, wifi_fullmac_hw_ctx_t *ctx)
{
	const wifi_fullmac_chipset_t *chip;

	chip = wifi_fullmac_chipset_match(vid, did);
	if (!chip || chip->bus != WIFI_FULLMAC_BUS_PCIE) {
		const char *force = getenv("FL_WIFI_FULLMAC_FORCE");
		if (!force || !force[0] || strcmp(force, "0") == 0) {
			char msg[160];
			snprintf(msg, sizeof(msg),
				 "PCI vid:did %04x:%04x not in ax PCIe table "
				 "(set FL_WIFI_FULLMAC_FORCE=1 to attach anyway)",
				 (unsigned)vid, (unsigned)did);
			wifi_fullmac_set_error(msg);
			return -1;
		}
		ctx->match.name = "unknown ax PCIe (forced)";
		ctx->match.vendor_id = vid;
		ctx->match.device_id = did;
		ctx->match.bus = WIFI_FULLMAC_BUS_PCIE;
	} else {
		ctx->match = *chip;
	}
	ctx->bus = WIFI_FULLMAC_BUS_PCIE;
	return 0;
}

static int pcie_ctx_from_bdf(uint8_t bus, uint8_t dev, uint8_t fn, wifi_fullmac_hw_ctx_t *ctx)
{
	uint16_t vid = 0, did = 0;

	if (!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	if (pcie_read_ids(bus, dev, fn, &vid, &did) != 0) {
		wifi_fullmac_set_error("PCI config read failed or empty device");
		return -1;
	}
	if (pcie_apply_chipset(vid, did, ctx) != 0)
		return -1;
	ctx->loc.pcie.bus = bus;
	ctx->loc.pcie.dev = dev;
	ctx->loc.pcie.fn = fn;
	ctx->loc.pcie.bar0 = pcie_decode_bar0(pci_read_config32(bus, dev, fn, 0x10u));
	return 0;
}

#if defined(__linux__)
static int pcie_scan_sysfs(wifi_fullmac_hw_ctx_t *ctx)
{
	DIR *d;
	struct dirent *de;
	const char *want = getenv("FL_WIFI_FULLMAC_VIDPID");
	uint16_t want_vid = 0, want_did = 0;

	if (!ctx)
		return -1;
	if (want && want[0]) {
		unsigned v = 0, id = 0;
		if (sscanf(want, "%x:%x", &v, &id) == 2) {
			want_vid = (uint16_t)v;
			want_did = (uint16_t)id;
		}
	}
	d = opendir("/sys/bus/pci/devices");
	if (!d) {
		wifi_fullmac_set_error("cannot open /sys/bus/pci/devices");
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		uint8_t bus, dev_fn, fn;
		uint16_t vid = 0, did = 0;

		if (de->d_name[0] == '.')
			continue;
		if (pcie_parse_bdf(de->d_name, &bus, &dev_fn, &fn) != 0)
			continue;
		if (pcie_read_ids(bus, dev_fn, fn, &vid, &did) != 0)
			continue;
		if (want_vid && (vid != want_vid || did != want_did))
			continue;
		memset(ctx, 0, sizeof(*ctx));
		if (pcie_apply_chipset(vid, did, ctx) != 0)
			continue;
		ctx->loc.pcie.bus = bus;
		ctx->loc.pcie.dev = dev_fn;
		ctx->loc.pcie.fn = fn;
		ctx->loc.pcie.bar0 =
			pcie_decode_bar0(pci_read_config32(bus, dev_fn, fn, 0x10u));
		closedir(d);
		return 0;
	}
	closedir(d);
	wifi_fullmac_set_error("no known 802.11ax PCI device found");
	return -1;
}
#endif

#if !defined(__linux__)
static int pcie_scan_sysfs(wifi_fullmac_hw_ctx_t *ctx)
{
	(void)ctx;
	wifi_fullmac_set_error("PCI auto-scan requires Linux sysfs");
	return -1;
}
#endif

int wifi_fullmac_pcie_probe_ctx(wifi_fullmac_hw_ctx_t *ctx)
{
	const char *bdf_env;

	if (!ctx)
		return -1;
	bdf_env = getenv("FL_WIFI_FULLMAC_PCI");
	if (bdf_env && bdf_env[0]) {
		uint8_t bus, dev, fn;
		if (pcie_parse_bdf(bdf_env, &bus, &dev, &fn) != 0 ||
		    pcie_ctx_from_bdf(bus, dev, fn, ctx) != 0)
			return -1;
		return 0;
	}

	{
		const char *want = getenv("FL_WIFI_FULLMAC_VIDPID");
		if (want && want[0]) {
			unsigned v = 0, id = 0;
			if (sscanf(want, "%x:%x", &v, &id) == 2) {
				const wifi_fullmac_chipset_t *chip =
					wifi_fullmac_chipset_match((uint16_t)v, (uint16_t)id);
				if (chip && chip->bus != WIFI_FULLMAC_BUS_PCIE) {
					wifi_fullmac_set_error(
						"FL_WIFI_FULLMAC_VIDPID matches a USB chipset; "
						"use FL_WIFI_FULLMAC_USB=bus-port or auto USB scan");
					return -1;
				}
			}
		}
	}
	return pcie_scan_sysfs(ctx);
}

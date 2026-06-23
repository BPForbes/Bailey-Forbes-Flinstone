/*
 * Phase 4 PCIe FullMAC: sysfs/PCI config probe, BAR decode, stub chipset ops.
 * Chipset-specific firmware/command rings land in follow-up drivers per VID:PID.
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_chipset.h"
#include "wifi_fullmac_fw.h"

#include "wifi_platform.h"

#include "fl/driver/pci.h"
#include "fl/mem_asm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#include <errno.h>
#endif

typedef struct {
	wifi_fullmac_chipset_t match;
	uint8_t bus;
	uint8_t dev;
	uint8_t fn;
	uint32_t bar0;
	uint8_t *fw;
	size_t fw_len;
	wifi_network_t scan_cache[32];
	uint16_t scan_count;
} wifi_fullmac_pcie_ctx_t;

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

static void pcie_fill_probe_info(const wifi_fullmac_pcie_ctx_t *ctx,
				 wifi_fullmac_probe_info_t *info)
{
	if (!ctx || !info)
		return;
	memset(info, 0, sizeof(*info));
	info->bus = ctx->bus;
	info->dev = ctx->dev;
	info->fn = ctx->fn;
	info->vendor_id = ctx->match.vendor_id;
	info->device_id = ctx->match.device_id;
	info->bar0 = ctx->bar0;
	snprintf(info->bdf, sizeof(info->bdf), "%02x:%02x.%x",
		 (unsigned)ctx->bus, (unsigned)ctx->dev, (unsigned)ctx->fn);
	strncpy(info->chipset, ctx->match.name ? ctx->match.name : "unknown",
		sizeof(info->chipset) - 1u);
}

static uint32_t pcie_decode_bar0(uint32_t bar0_raw)
{
	if (bar0_raw == 0u || bar0_raw == 0xffffffffu)
		return 0u;
	if ((bar0_raw & 0x1u) == 0u)
		return bar0_raw & ~0xFu; /* 32-bit MEM */
	return 0u; /* I/O BAR — not supported for MMIO firmware doorbell yet */
}

static int pcie_read_ids(uint8_t bus, uint8_t dev, uint8_t fn,
			 uint16_t *vid, uint16_t *did)
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

static int pcie_ctx_from_bdf(uint8_t bus, uint8_t dev, uint8_t fn,
			     wifi_fullmac_pcie_ctx_t *ctx)
{
	uint16_t vid = 0, did = 0;
	const wifi_fullmac_chipset_t *chip;

	if (!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	if (pcie_read_ids(bus, dev, fn, &vid, &did) != 0) {
		wifi_fullmac_set_error("PCI config read failed or empty device");
		return -1;
	}
	chip = wifi_fullmac_chipset_match(vid, did);
	if (!chip) {
		const char *force = getenv("FL_WIFI_FULLMAC_FORCE");
		if (!force || !force[0] || strcmp(force, "0") == 0) {
			char msg[128];
			snprintf(msg, sizeof(msg),
				 "PCI %02x:%02x.%x vid:did %04x:%04x not in ax chipset table "
				 "(set FL_WIFI_FULLMAC_FORCE=1 to attach anyway)",
				 (unsigned)bus, (unsigned)dev, (unsigned)fn,
				 (unsigned)vid, (unsigned)did);
			wifi_fullmac_set_error(msg);
			return -1;
		}
		ctx->match.name = "unknown ax (forced)";
		ctx->match.vendor_id = vid;
		ctx->match.device_id = did;
		ctx->match.bus = WIFI_FULLMAC_BUS_PCIE;
	} else {
		ctx->match = *chip;
	}
	ctx->bus = bus;
	ctx->dev = dev;
	ctx->fn = fn;
	ctx->bar0 = pcie_decode_bar0(pci_read_config32(bus, dev, fn, 0x10u));
	return 0;
}

#if defined(__linux__)
static int pcie_scan_sysfs(wifi_fullmac_pcie_ctx_t *ctx)
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
		const wifi_fullmac_chipset_t *chip;
		const char *force;

		if (de->d_name[0] == '.')
			continue;
		if (pcie_parse_bdf(de->d_name, &bus, &dev_fn, &fn) != 0)
			continue;
		if (pcie_read_ids(bus, dev_fn, fn, &vid, &did) != 0)
			continue;
		if (want_vid && (vid != want_vid || did != want_did))
			continue;
		chip = wifi_fullmac_chipset_match(vid, did);
		force = getenv("FL_WIFI_FULLMAC_FORCE");
		if (!chip && (!force || !force[0] || strcmp(force, "0") == 0))
			continue;
		memset(ctx, 0, sizeof(*ctx));
		if (chip)
			ctx->match = *chip;
		else {
			ctx->match.name = "unknown ax (forced)";
			ctx->match.vendor_id = vid;
			ctx->match.device_id = did;
			ctx->match.bus = WIFI_FULLMAC_BUS_PCIE;
		}
		ctx->bus = bus;
		ctx->dev = dev_fn;
		ctx->fn = fn;
		ctx->bar0 = pcie_decode_bar0(pci_read_config32(bus, dev_fn, fn, 0x10u));
		closedir(d);
		return 0;
	}
	closedir(d);
	wifi_fullmac_set_error("no known 802.11ax PCI device found (see wifi probe)");
	return -1;
}
#endif

#if !defined(__linux__)
static int pcie_scan_sysfs(wifi_fullmac_pcie_ctx_t *ctx)
{
	(void)ctx;
	wifi_fullmac_set_error("PCI auto-scan requires Linux sysfs");
	return -1;
}
#endif

static int pcie_stub_init(wifi_fullmac_t *dev)
{
	wifi_fullmac_pcie_ctx_t *ctx = (wifi_fullmac_pcie_ctx_t *)dev->driver_data;
	const char *fw_path;

	if (!ctx)
		return -1;
	dev->vendor_id = ctx->match.vendor_id;
	dev->device_id = ctx->match.device_id;
	dev->bus_type = ctx->match.bus;
	dev->bar0 = ctx->bar0;
	dev->state = WIFI_FULLMAC_STATE_INITIALIZING;

	fw_path = getenv("FL_WIFI_FULLMAC_FW");
	if (fw_path && fw_path[0]) {
		dev->state = WIFI_FULLMAC_STATE_FW_LOADING;
		if (wifi_fullmac_fw_load_file(fw_path, &ctx->fw, &ctx->fw_len) != 0) {
			wifi_fullmac_set_error("FL_WIFI_FULLMAC_FW load failed");
			dev->state = WIFI_FULLMAC_STATE_ERROR;
			return -1;
		}
		if (dev->ops && dev->ops->load_firmware &&
		    dev->ops->load_firmware(dev, ctx->fw, ctx->fw_len) != 0) {
			wifi_fullmac_set_error("firmware upload not implemented for this chipset");
			dev->state = WIFI_FULLMAC_STATE_ERROR;
			return -1;
		}
		dev->state = WIFI_FULLMAC_STATE_FW_READY;
	} else {
		dev->state = WIFI_FULLMAC_STATE_IDLE;
	}
	return 0;
}

static int pcie_stub_deinit(wifi_fullmac_t *dev)
{
	wifi_fullmac_pcie_ctx_t *ctx;

	if (!dev)
		return -1;
	ctx = (wifi_fullmac_pcie_ctx_t *)dev->driver_data;
	if (ctx && ctx->fw) {
		wifi_platform_free(ctx->fw);
		ctx->fw = NULL;
		ctx->fw_len = 0;
	}
	dev->state = WIFI_FULLMAC_STATE_DOWN;
	return 0;
}

static int pcie_stub_not_ready(wifi_fullmac_t *dev)
{
	(void)dev;
	wifi_fullmac_set_error("chipset firmware/command path not implemented yet");
	return -1;
}

static int pcie_stub_start_scan(wifi_fullmac_t *dev, const char *ssid)
{
	(void)ssid;
	if (!dev)
		return -1;
	if (dev->state < WIFI_FULLMAC_STATE_IDLE) {
		wifi_fullmac_set_error("FullMAC device not initialized");
		return -1;
	}
	dev->state = WIFI_FULLMAC_STATE_SCANNING;
	return pcie_stub_not_ready(dev);
}

static int pcie_stub_get_scan_results(wifi_fullmac_t *dev, wifi_network_t *networks,
				      uint16_t *count)
{
	wifi_fullmac_pcie_ctx_t *ctx;

	if (!dev || !networks || !count)
		return -1;
	ctx = (wifi_fullmac_pcie_ctx_t *)dev->driver_data;
	*count = 0;
	if (ctx && ctx->scan_count) {
		uint16_t n = ctx->scan_count;
		uint16_t i;
		if (n > *count)
			n = *count;
		for (i = 0; i < n; i++)
			networks[i] = ctx->scan_cache[i];
		*count = n;
		dev->state = WIFI_FULLMAC_STATE_IDLE;
		return 0;
	}
	return pcie_stub_not_ready(dev);
}

static int pcie_stub_deauth(wifi_fullmac_t *dev, uint16_t reason)
{
	(void)reason;
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static const wifi_fullmac_ops_t s_pcie_stub_ops = {
	.init = pcie_stub_init,
	.deinit = pcie_stub_deinit,
	.reset = pcie_stub_not_ready,
	.load_firmware = pcie_stub_not_ready,
	.send_command = pcie_stub_not_ready,
	.poll_event = pcie_stub_not_ready,
	.start_scan = pcie_stub_start_scan,
	.get_scan_results = pcie_stub_get_scan_results,
	.authenticate = pcie_stub_not_ready,
	.associate = pcie_stub_not_ready,
	.deauthenticate = pcie_stub_deauth,
	.set_key = pcie_stub_not_ready,
	.delete_key = pcie_stub_not_ready,
	.get_he_capabilities = pcie_stub_not_ready,
	.set_he_capabilities = pcie_stub_not_ready,
	.setup_twt = pcie_stub_not_ready,
	.teardown_twt = pcie_stub_not_ready,
	.tx_packet = pcie_stub_not_ready,
	.rx_packet = pcie_stub_not_ready,
};

static int pcie_attach_ctx(wifi_fullmac_pcie_ctx_t *pcie_in, wifi_fullmac_t **out_dev)
{
	wifi_fullmac_t *dev;
	wifi_fullmac_pcie_ctx_t *pcie;

	if (!pcie_in || !out_dev)
		return -1;
	pcie = (wifi_fullmac_pcie_ctx_t *)wifi_platform_malloc(sizeof(*pcie));
	if (!pcie)
		return -1;
	*pcie = *pcie_in;
	dev = wifi_fullmac_dev_alloc();
	if (!dev) {
		wifi_platform_free(pcie);
		return -1;
	}
	strncpy(dev->name, "wlan_ax0", sizeof(dev->name) - 1u);
	dev->ops = &s_pcie_stub_ops;
	dev->driver_data = pcie;
	if (wifi_fullmac_init(dev) != 0) {
		wifi_fullmac_dev_free(dev);
		wifi_platform_free(pcie);
		return -1;
	}
	*out_dev = dev;
	return 0;
}

int wifi_fullmac_hw_probe(wifi_fullmac_t **out_dev, wifi_fullmac_probe_info_t *info_out)
{
	wifi_fullmac_pcie_ctx_t pcie;
	const char *bdf_env;
	const char *enable;

	if (!out_dev)
		return -1;
	*out_dev = NULL;
	wifi_fullmac_set_error(NULL);
	memset(&pcie, 0, sizeof(pcie));

	enable = getenv("FL_WIFI_FULLMAC");
	bdf_env = getenv("FL_WIFI_FULLMAC_PCI");
	if ((!enable || !enable[0] || strcmp(enable, "0") == 0) &&
	    (!bdf_env || !bdf_env[0]) &&
	    (!getenv("FL_WIFI_FULLMAC_AUTO") ||
	     !getenv("FL_WIFI_FULLMAC_AUTO")[0] ||
	     strcmp(getenv("FL_WIFI_FULLMAC_AUTO"), "0") == 0) &&
	    (!getenv("FL_WIFI_FULLMAC_VIDPID") ||
	     !getenv("FL_WIFI_FULLMAC_VIDPID")[0])) {
		wifi_fullmac_set_error("set FL_WIFI_FULLMAC=1 or FL_WIFI_FULLMAC_PCI=b:d.f");
		return -1;
	}

	if (bdf_env && bdf_env[0]) {
		uint8_t bus, dev, fn;
		if (pcie_parse_bdf(bdf_env, &bus, &dev, &fn) != 0 ||
		    pcie_ctx_from_bdf(bus, dev, fn, &pcie) != 0)
			return -1;
	} else if (pcie_scan_sysfs(&pcie) != 0) {
		return -1;
	}

	if (info_out)
		pcie_fill_probe_info(&pcie, info_out);
	return pcie_attach_ctx(&pcie, out_dev);
}

void wifi_fullmac_hw_detach(wifi_fullmac_t *dev)
{
	wifi_fullmac_pcie_ctx_t *ctx;

	if (!dev)
		return;
	ctx = (wifi_fullmac_pcie_ctx_t *)dev->driver_data;
	(void)wifi_fullmac_deinit(dev);
	if (ctx) {
		if (ctx->fw)
			wifi_platform_free(ctx->fw);
		wifi_platform_free(ctx);
	}
	wifi_fullmac_dev_free(dev);
}

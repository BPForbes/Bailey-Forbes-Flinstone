/*
 * Phase 4 FullMAC hardware orchestration: shared stub ops, attach, probe dispatch.
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_fw.h"
#include "wifi_fullmac_hw_internal.h"

#include "wifi_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hw_stub_init(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_ctx_t *ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	const char *fw_path;

	if (!ctx)
		return -1;
	dev->vendor_id = ctx->match.vendor_id;
	dev->device_id = ctx->match.device_id;
	dev->bus_type = ctx->bus;
	if (ctx->bus == WIFI_FULLMAC_BUS_PCIE)
		dev->bar0 = ctx->loc.pcie.bar0;
	else
		dev->bar0 = 0u;
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

static int hw_stub_deinit(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!dev)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	if (ctx && ctx->fw) {
		wifi_platform_free(ctx->fw);
		ctx->fw = NULL;
		ctx->fw_len = 0;
	}
	dev->state = WIFI_FULLMAC_STATE_DOWN;
	return 0;
}

static int hw_stub_not_ready(wifi_fullmac_t *dev)
{
	(void)dev;
	wifi_fullmac_set_error("chipset firmware/command path not implemented yet");
	return -1;
}

static int hw_stub_start_scan(wifi_fullmac_t *dev, const char *ssid)
{
	(void)ssid;
	if (!dev)
		return -1;
	if (dev->state < WIFI_FULLMAC_STATE_IDLE) {
		wifi_fullmac_set_error("FullMAC device not initialized");
		return -1;
	}
	dev->state = WIFI_FULLMAC_STATE_SCANNING;
	return hw_stub_not_ready(dev);
}

static int hw_stub_get_scan_results(wifi_fullmac_t *dev, wifi_network_t *networks,
				    uint16_t *count)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!dev || !networks || !count)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	*count = 0;
	if (ctx && ctx->scan_count) {
		uint16_t n = ctx->scan_count;
		uint16_t i;

		for (i = 0; i < n; i++)
			networks[i] = ctx->scan_cache[i];
		*count = n;
		dev->state = WIFI_FULLMAC_STATE_IDLE;
		return 0;
	}
	return hw_stub_not_ready(dev);
}

static int hw_stub_deauth(wifi_fullmac_t *dev, uint16_t reason)
{
	(void)reason;
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static const wifi_fullmac_ops_t s_hw_stub_ops = {
	.init = hw_stub_init,
	.deinit = hw_stub_deinit,
	.reset = hw_stub_not_ready,
	.load_firmware = hw_stub_not_ready,
	.send_command = hw_stub_not_ready,
	.poll_event = hw_stub_not_ready,
	.start_scan = hw_stub_start_scan,
	.get_scan_results = hw_stub_get_scan_results,
	.authenticate = hw_stub_not_ready,
	.associate = hw_stub_not_ready,
	.deauthenticate = hw_stub_deauth,
	.set_key = hw_stub_not_ready,
	.delete_key = hw_stub_not_ready,
	.get_he_capabilities = hw_stub_not_ready,
	.set_he_capabilities = hw_stub_not_ready,
	.setup_twt = hw_stub_not_ready,
	.teardown_twt = hw_stub_not_ready,
	.tx_packet = hw_stub_not_ready,
	.rx_packet = hw_stub_not_ready,
};

const wifi_fullmac_ops_t *wifi_fullmac_hw_stub_ops(void)
{
	return &s_hw_stub_ops;
}

void wifi_fullmac_hw_fill_probe_info(const wifi_fullmac_hw_ctx_t *ctx,
				     wifi_fullmac_probe_info_t *info)
{
	if (!ctx || !info)
		return;
	memset(info, 0, sizeof(*info));
	info->bus = ctx->bus;
	info->vendor_id = ctx->match.vendor_id;
	info->device_id = ctx->match.device_id;
	strncpy(info->chipset, ctx->match.name ? ctx->match.name : "unknown",
		sizeof(info->chipset) - 1u);
	if (ctx->bus == WIFI_FULLMAC_BUS_PCIE) {
		info->pci_bus = ctx->loc.pcie.bus;
		info->pci_dev = ctx->loc.pcie.dev;
		info->pci_fn = ctx->loc.pcie.fn;
		info->bar0 = ctx->loc.pcie.bar0;
		snprintf(info->bdf, sizeof(info->bdf), "%02x:%02x.%x",
			 (unsigned)ctx->loc.pcie.bus, (unsigned)ctx->loc.pcie.dev,
			 (unsigned)ctx->loc.pcie.fn);
	} else if (ctx->bus == WIFI_FULLMAC_BUS_USB) {
		strncpy(info->usb_port, ctx->loc.usb.port, sizeof(info->usb_port) - 1u);
	}
}

int wifi_fullmac_hw_attach_ctx(wifi_fullmac_hw_ctx_t *ctx_in, wifi_fullmac_t **out_dev)
{
	wifi_fullmac_t *dev;
	wifi_fullmac_hw_ctx_t *ctx;

	if (!ctx_in || !out_dev)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)wifi_platform_malloc(sizeof(*ctx));
	if (!ctx)
		return -1;
	*ctx = *ctx_in;
	dev = wifi_fullmac_dev_alloc();
	if (!dev) {
		wifi_platform_free(ctx);
		return -1;
	}
	strncpy(dev->name, "wlan_ax0", sizeof(dev->name) - 1u);
	dev->ops = wifi_fullmac_hw_stub_ops();
	dev->driver_data = ctx;
	if (wifi_fullmac_init(dev) != 0) {
		wifi_fullmac_dev_free(dev);
		wifi_platform_free(ctx);
		return -1;
	}
	*out_dev = dev;
	return 0;
}

static int hw_probe_enabled(void)
{
	const char *enable = getenv("FL_WIFI_FULLMAC");
	const char *auto_env = getenv("FL_WIFI_FULLMAC_AUTO");

	if (enable && enable[0] && strcmp(enable, "0") != 0)
		return 1;
	if (getenv("FL_WIFI_FULLMAC_PCI") && getenv("FL_WIFI_FULLMAC_PCI")[0])
		return 1;
	if (getenv("FL_WIFI_FULLMAC_USB") && getenv("FL_WIFI_FULLMAC_USB")[0])
		return 1;
	if (getenv("FL_WIFI_FULLMAC_VIDPID") && getenv("FL_WIFI_FULLMAC_VIDPID")[0])
		return 1;
	if (auto_env && auto_env[0] && strcmp(auto_env, "0") != 0)
		return 1;
	return 0;
}

int wifi_fullmac_hw_probe(wifi_fullmac_t **out_dev, wifi_fullmac_probe_info_t *info_out)
{
	wifi_fullmac_hw_ctx_t ctx;
	const char *pci_env;
	const char *usb_env;
	int rc = -1;

	if (!out_dev)
		return -1;
	*out_dev = NULL;
	wifi_fullmac_set_error(NULL);
	memset(&ctx, 0, sizeof(ctx));

	if (!hw_probe_enabled()) {
		wifi_fullmac_set_error(
			"set FL_WIFI_FULLMAC=1, FL_WIFI_FULLMAC_PCI=b:d.f, or FL_WIFI_FULLMAC_USB=bus-port");
		return -1;
	}

	pci_env = getenv("FL_WIFI_FULLMAC_PCI");
	usb_env = getenv("FL_WIFI_FULLMAC_USB");
	if (pci_env && pci_env[0]) {
		rc = wifi_fullmac_pcie_probe_ctx(&ctx);
	} else if (usb_env && usb_env[0]) {
		rc = wifi_fullmac_usb_probe_ctx(&ctx);
	} else if (wifi_fullmac_pcie_probe_ctx(&ctx) == 0) {
		rc = 0;
	} else {
		memset(&ctx, 0, sizeof(ctx));
		rc = wifi_fullmac_usb_probe_ctx(&ctx);
	}

	if (rc != 0)
		return -1;
	if (info_out)
		wifi_fullmac_hw_fill_probe_info(&ctx, info_out);
	return wifi_fullmac_hw_attach_ctx(&ctx, out_dev);
}

void wifi_fullmac_hw_detach(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!dev)
		return;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	(void)wifi_fullmac_deinit(dev);
	if (ctx) {
		if (ctx->fw)
			wifi_platform_free(ctx->fw);
		wifi_platform_free(ctx);
	}
	wifi_fullmac_dev_free(dev);
}

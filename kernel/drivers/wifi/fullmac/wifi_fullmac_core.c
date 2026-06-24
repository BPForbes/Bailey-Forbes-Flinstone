/*
 * FullMAC core: netdev glue, lifecycle, chipset table, firmware load, connect.
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_chipset.h"

#ifndef FL_FULLMAC_CHIPSET_ONLY
#include "wifi_fullmac_hw_internal.h"
#include "wifi_platform.h"
#include "fl/mem_asm.h"
#include "kernel/core/net/net_wifi_mgmt.h"
#include "kernel/core/net/net_wire.h"
#include "wifi_driver_packet.h"
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_fullmac_last_error[256];

void wifi_fullmac_set_error(const char *msg)
{
	if (!msg || !msg[0]) {
		s_fullmac_last_error[0] = '\0';
		return;
	}
	strncpy(s_fullmac_last_error, msg, sizeof(s_fullmac_last_error) - 1u);
	s_fullmac_last_error[sizeof(s_fullmac_last_error) - 1u] = '\0';
}

const char *wifi_fullmac_last_error(void)
{
	return s_fullmac_last_error[0] ? s_fullmac_last_error : "";
}

#ifndef FL_FULLMAC_CHIPSET_ONLY

typedef struct {
	wifi_fullmac_t dev;
	fl_net_driver_t netdev;
} wifi_fullmac_dev_blob_t;

static wifi_fullmac_dev_blob_t *fullmac_blob_from_dev(wifi_fullmac_t *dev)
{
	if (!dev)
		return NULL;
	return (wifi_fullmac_dev_blob_t *)((char *)dev - offsetof(wifi_fullmac_dev_blob_t, dev));
}

static fl_result_t fullmac_netdev_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
	wifi_fullmac_dev_blob_t *blob;
	int rc;

	if (!drv || !frame)
		return FL_RESULT_INVAL;
	blob = (wifi_fullmac_dev_blob_t *)drv->impl;
	if (!blob || !blob->dev.ops || !blob->dev.ops->tx_packet)
		return FL_RESULT_NOSYS;
	if (wifi_driver_packet_validate_tx(frame->data, frame->len) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	rc = blob->dev.ops->tx_packet(&blob->dev, frame->data, frame->len);
	if (rc == 0) {
		blob->dev.frames_tx++;
		return FL_RESULT_OK;
	}
	blob->dev.errors++;
	return FL_RESULT_ERR;
}

static fl_result_t fullmac_netdev_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	wifi_fullmac_dev_blob_t *blob;
	size_t n = 0;
	int rc;
	fl_net_pipeline_rx_t pipe;

	if (!drv || !out)
		return FL_RESULT_INVAL;
	blob = (wifi_fullmac_dev_blob_t *)drv->impl;
	if (!blob || !blob->dev.ops || !blob->dev.ops->rx_packet)
		return FL_RESULT_NOSYS;
	rc = blob->dev.ops->rx_packet(&blob->dev, out->data, out->cap, &n);
	if (rc != 0)
		return FL_RESULT_TIMEDOUT;
	out->len = n;
	if (fl_net_wire_check_rx_fill(out, out->len) != FL_RESULT_OK ||
	    wifi_driver_packet_ingest_rx(out->data, out->len, &pipe) != FL_RESULT_OK) {
		blob->dev.errors++;
		return FL_RESULT_ERR;
	}
	blob->dev.frames_rx++;
	return FL_RESULT_OK;
}

wifi_fullmac_t *wifi_fullmac_dev_alloc(void)
{
	wifi_fullmac_dev_blob_t *blob;

	blob = (wifi_fullmac_dev_blob_t *)wifi_platform_malloc(sizeof(*blob));
	if (!blob)
		return NULL;
	asm_mem_zero(blob, sizeof(*blob));
	blob->dev.netdev = &blob->netdev;
	blob->netdev.send = fullmac_netdev_send;
	blob->netdev.recv = fullmac_netdev_recv;
	blob->netdev.mtu = FL_NET_ETH_MTU_DEFAULT;
	blob->netdev.impl = blob;
	blob->dev.state = WIFI_FULLMAC_STATE_DOWN;
	return &blob->dev;
}

void wifi_fullmac_dev_free(wifi_fullmac_t *dev)
{
	wifi_fullmac_dev_blob_t *blob = fullmac_blob_from_dev(dev);

	if (!blob)
		return;
	wifi_platform_free(blob);
}

int wifi_fullmac_init(wifi_fullmac_t *dev)
{
	if (!dev || !dev->ops || !dev->ops->init)
		return -1;
	return dev->ops->init(dev);
}

int wifi_fullmac_deinit(wifi_fullmac_t *dev)
{
	if (!dev || !dev->ops || !dev->ops->deinit)
		return -1;
	return dev->ops->deinit(dev);
}

int wifi_fullmac_get_he_capabilities(wifi_fullmac_t *dev, wifi_fullmac_he_cap_t *he_cap)
{
	if (!dev || !he_cap || !dev->ops || !dev->ops->get_he_capabilities)
		return -1;
	return dev->ops->get_he_capabilities(dev, he_cap);
}

int wifi_fullmac_setup_twt(wifi_fullmac_t *dev, const wifi_fullmac_twt_setup_t *twt)
{
	if (!dev || !twt || !dev->ops || !dev->ops->setup_twt)
		return -1;
	return dev->ops->setup_twt(dev, twt);
}

int wifi_fullmac_pcie_create(uint16_t vendor_id, uint16_t device_id, wifi_fullmac_t **out_dev)
{
	(void)vendor_id;
	(void)device_id;
	if (!out_dev)
		return -1;
	*out_dev = NULL;
	wifi_fullmac_set_error("wifi_fullmac_pcie_create: use wifi_fullmac_hw_probe()");
	return -1;
}

int wifi_fullmac_pcie_destroy(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_detach(dev);
	return 0;
}

#endif /* !FL_FULLMAC_CHIPSET_ONLY */

/* --- Chipset ID table --- */

static const wifi_fullmac_chipset_t s_chipsets[] = {
	{ "Intel AX200",         0x8086u, 0x2723u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Intel AX210",         0x8086u, 0x2725u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Intel AX211",         0x8086u, 0x51f0u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Qualcomm QCA6390",    0x17cbu, 0x1101u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Qualcomm WCN6855",    0x17cbu, 0x1103u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "MediaTek MT7921",     0x14c3u, 0x7961u, WIFI_FULLMAC_BUS_PCIE,
	  "mediatek/WIFI_RAM_CODE_MT7921_1.bin" },
	{ "MediaTek MT7921AU",   0x0e8du, 0x7961u, WIFI_FULLMAC_BUS_USB,
	  "mediatek/WIFI_RAM_CODE_MT7921_1.bin" },
	{ "MediaTek MT7922",     0x14c3u, 0x0616u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Realtek RTL8852AE",   0x10ecu, 0x8852u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Realtek RTL8852BE",   0x10ecu, 0xb852u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ "Broadcom BCM4377",    0x14e4u, 0x4499u, WIFI_FULLMAC_BUS_PCIE, NULL },
	{ NULL,                  0u,      0u,      WIFI_FULLMAC_BUS_PCIE, NULL },
};

const wifi_fullmac_chipset_t *wifi_fullmac_chipset_match(uint16_t vendor_id,
							 uint16_t device_id)
{
	size_t i;

	for (i = 0; s_chipsets[i].name; i++) {
		if (s_chipsets[i].vendor_id == vendor_id &&
		    s_chipsets[i].device_id == device_id)
			return &s_chipsets[i];
	}
	return NULL;
}

const wifi_fullmac_chipset_t *wifi_fullmac_chipset_table(size_t *count_out)
{
	size_t n = 0;

	while (s_chipsets[n].name)
		n++;
	if (count_out)
		*count_out = n;
	return s_chipsets;
}

#ifndef FL_FULLMAC_CHIPSET_ONLY

int wifi_fullmac_fw_load_file(const char *path, uint8_t **out_buf, size_t *out_len)
{
	FILE *fp;
	long sz;
	uint8_t *buf;

	if (!path || !path[0] || !out_buf || !out_len)
		return -1;
	*out_buf = NULL;
	*out_len = 0;

	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return -1;
	}
	sz = ftell(fp);
	if (sz <= 0 || sz > (long)(32u * 1024u * 1024u)) {
		fclose(fp);
		return -1;
	}
	rewind(fp);
	buf = (uint8_t *)wifi_platform_malloc((size_t)sz);
	if (!buf) {
		fclose(fp);
		return -1;
	}
	if (fread(buf, 1u, (size_t)sz, fp) != (size_t)sz) {
		wifi_platform_free(buf);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	*out_buf = buf;
	*out_len = (size_t)sz;
	return 0;
}

int wifi_fullmac_station_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred)
{
	wifi_network_t networks[32];
	uint16_t count = 32;
	size_t i;
	const wifi_network_t *ap = NULL;
	wifi_auth_mode_t auth;

	if (!dev || !cred || !cred->ssid[0] || !dev->ops)
		return -1;
	if (dev->state < WIFI_FULLMAC_STATE_IDLE) {
		wifi_fullmac_set_error("FullMAC not ready for connect");
		return -1;
	}
	if (dev->ops->start_scan(dev, NULL) != 0)
		return -1;
	if (dev->ops->get_scan_results(dev, networks, &count) != 0)
		return -1;
	for (i = 0; i < (size_t)count; i++) {
		if (!strcmp(networks[i].ssid, cred->ssid)) {
			ap = &networks[i];
			break;
		}
	}
	if (!ap) {
		wifi_fullmac_set_error("SSID not found in FullMAC scan results");
		return -1;
	}
	switch (cred->auth_mode ? cred->auth_mode : FL_WIFI_AUTH_WPA2_PSK) {
	case FL_WIFI_AUTH_OPEN:
		auth = WIFI_AUTH_OPEN;
		break;
	case FL_WIFI_AUTH_WPA3_SAE:
		auth = WIFI_AUTH_WPA3_SAE;
		break;
	default:
		auth = WIFI_AUTH_WPA2_PSK;
		break;
	}
	if (auth != WIFI_AUTH_OPEN && cred->passphrase[0] == '\0') {
		wifi_fullmac_set_error("passphrase required");
		return -1;
	}
	if (dev->ops->authenticate &&
	    dev->ops->authenticate(dev, ap->bssid, (uint16_t)auth, 1) != 0)
		return -1;
	if (dev->ops->associate && dev->ops->associate(dev, ap->bssid) != 0)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_CONNECTED;
	return 0;
}

#endif /* !FL_FULLMAC_CHIPSET_ONLY */

/*
 * FullMAC core: netdev glue, device lifecycle, shared error channel.
 */

#include "wifi_fullmac.h"

#include "wifi_platform.h"

#include "fl/mem_asm.h"
#include "kernel/core/net/net_wire.h"
#include "wifi_driver_packet.h"

#include <stddef.h>
#include <stdio.h>
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

/*
 * WiFi Coprocessor Abstraction Layer Implementation
 * Platform-agnostic WiFi device management for 802.11ax support
 */

#include <string.h>
#include <stdlib.h>

#include "kernel/drivers/wifi_coprocessor.h"
#include "kernel/core/net/net_netdev.h"

/* Debug logging (disable in production) */
#define WIFI_COPROC_DEBUG 0

#if WIFI_COPROC_DEBUG
#include <stdio.h>
#define WIFI_COPROC_LOG(fmt, ...) printf("[WIFI] " fmt "\n", ##__VA_ARGS__)
#else
#define WIFI_COPROC_LOG(fmt, ...) ((void)0)
#endif

/* Forward declarations */
static int wifi_coproc_netdev_open(netdev_t *dev);
static int wifi_coproc_netdev_close(netdev_t *dev);
static int wifi_coproc_netdev_transmit(netdev_t *dev, const uint8_t *data,
				       size_t length);
static void wifi_coproc_netdev_poll(netdev_t *dev);

static const netdev_ops_t wifi_coproc_netdev_ops = {
	.open = wifi_coproc_netdev_open,
	.close = wifi_coproc_netdev_close,
	.transmit = wifi_coproc_netdev_transmit,
	.poll = wifi_coproc_netdev_poll,
};

int wifi_coproc_create(const char *name, wifi_coproc_t **out_coproc)
{
	if (!name || !out_coproc) {
		return -1;
	}

	wifi_coproc_t *coproc = (wifi_coproc_t *)malloc(sizeof(wifi_coproc_t));
	if (!coproc) {
		return -1;
	}

	memset(coproc, 0, sizeof(wifi_coproc_t));
	strncpy(coproc->name, name, WIFI_COPROC_NAME_MAX - 1);
	coproc->name[WIFI_COPROC_NAME_MAX - 1] = '\0';
	coproc->status = WIFI_STATUS_DOWN;

	/* Create backing netdev */
	coproc->netdev =
		netdev_create(name, NETDEV_TYPE_WIFI, &wifi_coproc_netdev_ops);
	if (!coproc->netdev) {
		free(coproc);
		return -1;
	}
	coproc->netdev->driver_data = coproc;

	*out_coproc = coproc;
	return 0;
}

int wifi_coproc_destroy(wifi_coproc_t *coproc)
{
	if (!coproc) {
		return -1;
	}

	if (coproc->netdev) {
		netdev_destroy(coproc->netdev);
	}

	free(coproc);
	return 0;
}

int wifi_coproc_register_ops(wifi_coproc_t *coproc, const wifi_coproc_ops_t *ops)
{
	if (!coproc || !ops) {
		return -1;
	}
	coproc->ops = ops;
	return 0;
}

int wifi_coproc_register_transport(wifi_coproc_t *coproc, void *transport_data)
{
	if (!coproc) {
		return -1;
	}
	coproc->transport_data = transport_data;
	return 0;
}

int wifi_coproc_init(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->ops || !coproc->ops->init) {
		WIFI_COPROC_LOG("ERROR: invalid coprocessor or ops");
		return -1;
	}

	WIFI_COPROC_LOG("Initializing coprocessor: %s", coproc->name);

	coproc->status = WIFI_STATUS_INITIALIZING;
	int ret = coproc->ops->init(coproc);

	if (ret != 0) {
		WIFI_COPROC_LOG("ERROR: init failed with code %d", ret);
		coproc->status = WIFI_STATUS_ERROR;
		coproc->errors++;
		return ret;
	}

	WIFI_COPROC_LOG("Coprocessor initialized successfully");
	return 0;
}

int wifi_coproc_deinit(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->ops || !coproc->ops->deinit) {
		return -1;
	}

	int ret = coproc->ops->deinit(coproc);
	if (ret == 0) {
		coproc->status = WIFI_STATUS_DOWN;
	}
	return ret;
}

int wifi_coproc_scan(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->ops || !coproc->ops->start_scan) {
		return -1;
	}
	return coproc->ops->start_scan(coproc);
}

int wifi_coproc_join(wifi_coproc_t *coproc, const char *ssid, const char *password,
		     wifi_auth_mode_t auth)
{
	if (!coproc || !coproc->ops || !coproc->ops->join_network) {
		return -1;
	}

	wifi_join_params_t params;
	memset(&params, 0, sizeof(params));
	if (ssid) {
		strncpy(params.ssid, ssid, WIFI_SSID_MAX);
	}
	if (password) {
		strncpy(params.password, password, WIFI_PASSWORD_MAX);
	}
	params.auth_mode = auth;
	params.channel = 0; /* Auto */

	return coproc->ops->join_network(coproc, &params);
}

int wifi_coproc_disconnect(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->ops || !coproc->ops->leave_network) {
		return -1;
	}
	return coproc->ops->leave_network(coproc);
}

wifi_coproc_status_t wifi_coproc_get_status(wifi_coproc_t *coproc)
{
	if (!coproc) {
		return WIFI_STATUS_ERROR;
	}
	if (coproc->ops && coproc->ops->get_status) {
		return coproc->ops->get_status(coproc);
	}
	return coproc->status;
}

const char *wifi_coproc_status_str(wifi_coproc_status_t status)
{
	switch (status) {
	case WIFI_STATUS_DOWN:
		return "DOWN";
	case WIFI_STATUS_INITIALIZING:
		return "INITIALIZING";
	case WIFI_STATUS_FIRMWARE_READY:
		return "FIRMWARE_READY";
	case WIFI_STATUS_SCANNING:
		return "SCANNING";
	case WIFI_STATUS_SCAN_COMPLETE:
		return "SCAN_COMPLETE";
	case WIFI_STATUS_AUTHENTICATING:
		return "AUTHENTICATING";
	case WIFI_STATUS_ASSOCIATING:
		return "ASSOCIATING";
	case WIFI_STATUS_ASSOCIATED:
		return "ASSOCIATED";
	case WIFI_STATUS_CONNECTED:
		return "CONNECTED";
	case WIFI_STATUS_DISCONNECTING:
		return "DISCONNECTING";
	case WIFI_STATUS_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

/* ============================================================================
 * netdev integration
 * ============================================================================
 */

static int wifi_coproc_netdev_open(netdev_t *dev)
{
	if (!dev || !dev->driver_data) {
		return -1;
	}

	wifi_coproc_t *coproc = (wifi_coproc_t *)dev->driver_data;
	return wifi_coproc_init(coproc);
}

static int wifi_coproc_netdev_close(netdev_t *dev)
{
	if (!dev || !dev->driver_data) {
		return -1;
	}

	wifi_coproc_t *coproc = (wifi_coproc_t *)dev->driver_data;
	return wifi_coproc_deinit(coproc);
}

static int wifi_coproc_netdev_transmit(netdev_t *dev, const uint8_t *data,
				       size_t length)
{
	if (!dev || !dev->driver_data || !data) {
		return -1;
	}

	wifi_coproc_t *coproc = (wifi_coproc_t *)dev->driver_data;

	if (!coproc->ops || !coproc->ops->send_packet) {
		return -1;
	}

	int ret = coproc->ops->send_packet(coproc, data, length);
	if (ret == 0) {
		coproc->frames_tx++;
	}
	return ret;
}

static void wifi_coproc_netdev_poll(netdev_t *dev)
{
	if (!dev || !dev->driver_data) {
		return;
	}

	wifi_coproc_t *coproc = (wifi_coproc_t *)dev->driver_data;

	if (coproc->ops && coproc->ops->poll) {
		coproc->ops->poll(coproc);
	}
}

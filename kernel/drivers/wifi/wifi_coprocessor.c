/*
 * WiFi Coprocessor Abstraction Layer Implementation
 * Platform-agnostic WiFi device management for 802.11ax support
 */

#include <string.h>

#include "wifi_coprocessor.h"
#include "wifi_driver_packet.h"
#include "wifi_platform.h"
#include "kernel/core/net/net_wire.h"

#include "fl/mem_asm.h"

/* Debug logging (disable in production) */
#define WIFI_COPROC_DEBUG 0

#if WIFI_COPROC_DEBUG
#include <stdio.h>
#define WIFI_COPROC_LOG(fmt, ...) printf("[WIFI] " fmt "\n", ##__VA_ARGS__)
#else
#define WIFI_COPROC_LOG(fmt, ...) ((void)0)
#endif

static wifi_coproc_t *wifi_coproc_from_driver(const fl_net_driver_t *drv)
{
	if (!drv)
		return NULL;
	return (wifi_coproc_t *)drv->impl;
}

static fl_result_t wifi_coproc_driver_send(fl_net_driver_t *drv,
					   const fl_net_frame_view_t *frame)
{
	wifi_coproc_t *coproc;
	int ret;

	if (!drv || !frame)
		return FL_RESULT_INVAL;
	coproc = wifi_coproc_from_driver(drv);
	if (!coproc || !coproc->ops || !coproc->ops->send_packet)
		return FL_RESULT_NOSYS;
	if (wifi_driver_packet_validate_tx(frame->data, frame->len) != FL_RESULT_OK)
		return FL_RESULT_INVAL;

	ret = coproc->ops->send_packet(coproc, frame->data, frame->len);
	if (ret == 0) {
		coproc->frames_tx++;
		return FL_RESULT_OK;
	}
	coproc->errors++;
	return FL_RESULT_ERR;
}

static fl_result_t wifi_coproc_driver_recv(fl_net_driver_t *drv,
					   fl_net_frame_mut_t *out)
{
	wifi_coproc_t *coproc;
	size_t out_len = 0;
	int ret;
	fl_net_pipeline_rx_t pipe;

	if (!drv || !out)
		return FL_RESULT_INVAL;
	coproc = wifi_coproc_from_driver(drv);
	if (!coproc || !coproc->ops || !coproc->ops->receive_packet)
		return FL_RESULT_NOSYS;

	fl_result_t wire_rc;
	fl_result_t ingest_rc;

	ret = coproc->ops->receive_packet(coproc, out->data, out->cap, &out_len);
	if (ret != 0)
		return FL_RESULT_TIMEDOUT;
	out->len = out_len;
	wire_rc = fl_net_wire_check_rx_fill(out, out->len);
	ingest_rc = wifi_driver_packet_ingest_rx(out->data, out->len, &pipe);
	if (wire_rc != FL_RESULT_OK || ingest_rc != FL_RESULT_OK) {
		coproc->errors++;
		return wire_rc != FL_RESULT_OK ? wire_rc : ingest_rc;
	}
	coproc->frames_rx++;
	return FL_RESULT_OK;
}

int wifi_coproc_create(const char *name, wifi_coproc_t **out_coproc)
{
	fl_net_driver_t *netdev;
	wifi_coproc_t *coproc;

	if (!name || !out_coproc)
		return -1;

	coproc = (wifi_coproc_t *)wifi_platform_malloc(sizeof(wifi_coproc_t));
	if (!coproc)
		return -1;

	asm_mem_zero(coproc, sizeof(*coproc));
	strncpy(coproc->name, name, WIFI_COPROC_NAME_MAX - 1);
	coproc->name[WIFI_COPROC_NAME_MAX - 1] = '\0';
	coproc->status = WIFI_STATUS_DOWN;

	netdev = (fl_net_driver_t *)wifi_platform_malloc(sizeof(fl_net_driver_t));
	if (!netdev) {
		wifi_platform_free(coproc);
		return -1;
	}
	asm_mem_zero(netdev, sizeof(*netdev));
	netdev->send = wifi_coproc_driver_send;
	netdev->recv = wifi_coproc_driver_recv;
	netdev->mtu = FL_NET_ETH_MTU_DEFAULT;
	netdev->impl = coproc;
	coproc->netdev = netdev;

	*out_coproc = coproc;
	return 0;
}

int wifi_coproc_destroy(wifi_coproc_t *coproc)
{
	if (!coproc)
		return -1;

	if (coproc->ops && coproc->ops->deinit)
		(void)coproc->ops->deinit(coproc);

	if (coproc->transport_owned && coproc->transport_data) {
		wifi_platform_free(coproc->transport_data);
		coproc->transport_data = NULL;
		coproc->transport_owned = false;
	}

	if (coproc->netdev) {
		wifi_platform_free(coproc->netdev);
		coproc->netdev = NULL;
	}

	wifi_platform_free(coproc);
	return 0;
}

int wifi_coproc_register_ops(wifi_coproc_t *coproc, const wifi_coproc_ops_t *ops)
{
	if (!coproc || !ops)
		return -1;
	coproc->ops = ops;
	return 0;
}

int wifi_coproc_register_transport(wifi_coproc_t *coproc, void *transport_data)
{
	if (!coproc)
		return -1;
	coproc->transport_data = transport_data;
	coproc->transport_owned = false;
	return 0;
}

int wifi_coproc_init(wifi_coproc_t *coproc)
{
	int ret;

	if (!coproc || !coproc->ops || !coproc->ops->init) {
		WIFI_COPROC_LOG("ERROR: invalid coprocessor or ops");
		return -1;
	}

	WIFI_COPROC_LOG("Initializing coprocessor: %s", coproc->name);

	coproc->status = WIFI_STATUS_INITIALIZING;
	ret = coproc->ops->init(coproc);

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
	int ret;

	if (!coproc || !coproc->ops || !coproc->ops->deinit)
		return -1;

	ret = coproc->ops->deinit(coproc);
	if (ret == 0)
		coproc->status = WIFI_STATUS_DOWN;
	return ret;
}

int wifi_coproc_scan(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->ops || !coproc->ops->start_scan)
		return -1;
	return coproc->ops->start_scan(coproc);
}

int wifi_coproc_get_scan_results(wifi_coproc_t *coproc, wifi_network_t *networks,
				 uint16_t *count)
{
	if (!coproc || !coproc->ops || !coproc->ops->get_scan_results)
		return -1;
	return coproc->ops->get_scan_results(coproc, networks, count);
}

int wifi_coproc_join(wifi_coproc_t *coproc, const char *ssid, const char *password,
		     wifi_auth_mode_t auth)
{
	wifi_join_params_t params;

	if (!coproc || !coproc->ops || !coproc->ops->join_network)
		return -1;

	asm_mem_zero(&params, sizeof(params));
	if (ssid)
		strncpy(params.ssid, ssid, WIFI_SSID_MAX);
	if (password)
		strncpy(params.password, password, WIFI_PASSWORD_MAX);
	params.auth_mode = auth;
	params.channel = 0;

	return coproc->ops->join_network(coproc, &params);
}

int wifi_coproc_disconnect(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->ops || !coproc->ops->leave_network)
		return -1;
	return coproc->ops->leave_network(coproc);
}

wifi_coproc_status_t wifi_coproc_get_status(wifi_coproc_t *coproc)
{
	if (!coproc)
		return WIFI_STATUS_ERROR;
	if (coproc->ops && coproc->ops->get_status)
		return coproc->ops->get_status(coproc);
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

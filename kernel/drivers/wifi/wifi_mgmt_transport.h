/*
 * Management-frame transport for in-tree Wi-Fi OTA (#328).
 * Kernel orchestrates; drivers execute TX/RX through this narrow boundary.
 */

#ifndef KERNEL_DRIVERS_WIFI_MGMT_TRANSPORT_H
#define KERNEL_DRIVERS_WIFI_MGMT_TRANSPORT_H

#include "wifi_coprocessor.h"

#include <stddef.h>
#include <stdint.h>

#define WIFI_OTA_MGMT_Q 4u
#define WIFI_OTA_DATA_Q 4u
#define WIFI_OTA_FRAME_MAX 512u

typedef struct {
	const wifi_network_t *ap;
	const uint8_t *sta_mac;
} wifi_mgmt_transport_mock_cfg_t;

typedef struct {
	wifi_mgmt_transport_mock_cfg_t cfg;
	uint8_t mgmt_rx[WIFI_OTA_MGMT_Q][WIFI_OTA_FRAME_MAX];
	size_t mgmt_rx_len[WIFI_OTA_MGMT_Q];
	unsigned mgmt_rx_count;
	uint8_t data_rx[WIFI_OTA_DATA_Q][WIFI_OTA_FRAME_MAX];
	size_t data_rx_len[WIFI_OTA_DATA_Q];
	unsigned data_rx_count;
	uint8_t eapol_rx_pending;
} wifi_mgmt_transport_mock_ctx_t;

typedef struct wifi_mgmt_transport wifi_mgmt_transport_t;

struct wifi_mgmt_transport {
	void *ctx;
	int (*tx_mgmt)(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len);
	int (*rx_mgmt)(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out,
		       unsigned timeout_ms);
	int (*tx_data)(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len);
	int (*rx_data)(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out,
		       unsigned timeout_ms);
};

int wifi_mgmt_transport_mock_init(wifi_mgmt_transport_t *tr, void *ctx_storage,
				  const wifi_mgmt_transport_mock_cfg_t *cfg);
void wifi_mgmt_transport_mock_reset(wifi_mgmt_transport_t *tr);

/** Size of caller-provided storage for wifi_mgmt_transport_mock_init. */
size_t wifi_mgmt_transport_mock_ctx_size(void);

#endif /* KERNEL_DRIVERS_WIFI_MGMT_TRANSPORT_H */

/*
 * Shared production-tail connect: mgmt OTA + supplicant + optional key install (#328).
 */

#ifndef KERNEL_DRIVERS_WIFI_CONNECT_OTA_H
#define KERNEL_DRIVERS_WIFI_CONNECT_OTA_H

#include "contract_p3_wifi.h"
#include "wifi_coprocessor.h"
#include "wifi_mgmt_transport.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
	int (*set_key)(void *ctx, const uint8_t *key, size_t key_len);
	void *ctx;
} wifi_connect_ota_hooks_t;

typedef enum {
	WIFI_CONNECT_OTA_ALL = 0,
	WIFI_CONNECT_OTA_AUTH_ONLY,
	WIFI_CONNECT_OTA_ASSOC_ONLY,
} wifi_connect_ota_phase_t;

int wifi_connect_ota_run(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			 const uint8_t sta_mac[6], wifi_mgmt_transport_t *tr,
			 const wifi_connect_ota_hooks_t *hooks);

int wifi_connect_ota_run_phase(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			       const uint8_t sta_mac[6], wifi_mgmt_transport_t *tr,
			       const wifi_connect_ota_hooks_t *hooks,
			       wifi_connect_ota_phase_t phase);

#endif /* KERNEL_DRIVERS_WIFI_CONNECT_OTA_H */

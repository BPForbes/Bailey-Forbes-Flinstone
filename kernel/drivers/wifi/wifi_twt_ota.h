/*
 * TWT Individual Setup/Teardown over management-frame OTA transport (#328).
 */

#ifndef KERNEL_DRIVERS_WIFI_TWT_OTA_H
#define KERNEL_DRIVERS_WIFI_TWT_OTA_H

#include "contract_p3_wifi.h"
#include "wifi_mgmt_transport.h"

#include <stdint.h>

#define WIFI_TWT_OTA_TIMEOUT_MS 5000u

int wifi_twt_ota_setup(const uint8_t sta_mac[6], const uint8_t bssid[6],
		       const fl_net_wifi_twt_params_t *req, fl_net_wifi_twt_params_t *agreed_out,
		       wifi_mgmt_transport_t *tr);

int wifi_twt_ota_teardown(const uint8_t sta_mac[6], const uint8_t bssid[6], uint8_t flow_id,
			  wifi_mgmt_transport_t *tr);

#endif /* KERNEL_DRIVERS_WIFI_TWT_OTA_H */

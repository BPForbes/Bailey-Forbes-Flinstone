/*
 * WiFi Driver Backend Integration
 * Bridges v4.3.0 drivers to contract_p3_wifi.h
 * Provides first-principles implementation without external library dependencies
 *
 * Integrates:
 * - Phase 1: ESP32/ESP8266 coprocessor over UART (primary backend)
 * - Phase 2: Open network + DHCP framework
 * - Phase 3: WPA2/WPA3-SAE supplicant
 * - Phase 4: FullMAC WiFi 6 (PCIe/SDIO, architecture ready for hardware)
 */

#ifndef KERNEL_DRIVERS_WIFI_DRIVER_BACKEND_H
#define KERNEL_DRIVERS_WIFI_DRIVER_BACKEND_H

#include "contract_p3_wifi.h"
#include "fl/result.h"

#include <stdint.h>
#include <stddef.h>

/* Backend type: determines active driver path */
typedef enum {
	WIFI_BACKEND_NONE = 0,      /* No driver active */
	WIFI_BACKEND_COPROCESSOR,   /* Phase 1: ESP32/ESP8266 UART */
	WIFI_BACKEND_FULLMAC,       /* Phase 4: Real WiFi 6 NIC (PCIe/SDIO) */
	WIFI_BACKEND_QEMU,          /* QEMU emulated 802.11ax */
} wifi_backend_type_t;

/* Initialize WiFi driver backend (Phase 1 by default if hardware available) */
fl_result_t wifi_driver_backend_init(void);

/* Query which backend is active */
wifi_backend_type_t wifi_driver_backend_active(void);

/* Public API: routes to active backend */
fl_result_t wifi_driver_scan(uint8_t band, unsigned timeout_ms);
fl_result_t wifi_driver_scan_result(fl_net_wifi_scan_entry_t *entries,
				    size_t cap, size_t *count_out);
fl_result_t wifi_driver_connect(const fl_net_wifi_cred_t *cred,
				unsigned timeout_ms);
fl_result_t wifi_driver_disconnect(void);
fl_net_wifi_state_t wifi_driver_state(void);
fl_result_t wifi_driver_he_cap(fl_net_wifi_he_cap_t *cap_out);
fl_result_t wifi_driver_twt_setup(const fl_net_wifi_twt_params_t *req,
				  fl_net_wifi_twt_params_t *agreed_out);
fl_result_t wifi_driver_twt_teardown(uint8_t flow_id);

/* Get netdev for routing/DHCP composition */
fl_net_driver_t *wifi_driver_netdev(void);

#endif /* KERNEL_DRIVERS_WIFI_DRIVER_BACKEND_H */

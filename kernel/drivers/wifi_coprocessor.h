/*
 * WiFi Coprocessor Abstraction Layer
 * Defines the interface for WiFi coprocessor devices (ESP32, ESP8266, etc.)
 * that operate as standalone MAC units communicating via UART/SPI/USB
 */

#ifndef KERNEL_DRIVERS_WIFI_COPROCESSOR_H
#define KERNEL_DRIVERS_WIFI_COPROCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "kernel/core/net/net_netdev.h"

#define WIFI_COPROC_NAME_MAX     16
#define WIFI_SSID_MAX            32
#define WIFI_PASSWORD_MAX        64
#define WIFI_BSSID_LEN           6
#define WIFI_MAX_NETWORKS        64

typedef enum {
	WIFI_STATUS_DOWN,
	WIFI_STATUS_INITIALIZING,
	WIFI_STATUS_FIRMWARE_READY,
	WIFI_STATUS_SCANNING,
	WIFI_STATUS_SCAN_COMPLETE,
	WIFI_STATUS_AUTHENTICATING,
	WIFI_STATUS_ASSOCIATING,
	WIFI_STATUS_ASSOCIATED,
	WIFI_STATUS_CONNECTED,
	WIFI_STATUS_DISCONNECTING,
	WIFI_STATUS_ERROR
} wifi_coproc_status_t;

typedef enum {
	WIFI_AUTH_OPEN,
	WIFI_AUTH_WEP,
	WIFI_AUTH_WPA_PSK,
	WIFI_AUTH_WPA2_PSK,
	WIFI_AUTH_WPA3_SAE,
	WIFI_AUTH_UNKNOWN
} wifi_auth_mode_t;

typedef struct {
	char ssid[WIFI_SSID_MAX + 1];
	uint8_t bssid[WIFI_BSSID_LEN];
	int8_t rssi;                    /* Signal strength in dBm */
	uint8_t channel;
	wifi_auth_mode_t auth_mode;
	uint32_t freq;                  /* Frequency in MHz */
} wifi_network_t;

typedef struct {
	char ssid[WIFI_SSID_MAX + 1];
	char password[WIFI_PASSWORD_MAX + 1];
	wifi_auth_mode_t auth_mode;
	uint8_t channel;                /* 0 = auto */
	bool save_credentials;
} wifi_join_params_t;

typedef struct wifi_coproc wifi_coproc_t;

/* Coprocessor operations */
typedef struct {
	/* Lifecycle */
	int (*init)(wifi_coproc_t *coproc);
	int (*deinit)(wifi_coproc_t *coproc);
	int (*reset)(wifi_coproc_t *coproc);

	/* State management */
	wifi_coproc_status_t (*get_status)(wifi_coproc_t *coproc);
	int (*set_mode)(wifi_coproc_t *coproc, uint8_t mode); /* STA, AP, etc. */

	/* Scanning & discovery */
	int (*start_scan)(wifi_coproc_t *coproc);
	int (*get_scan_results)(wifi_coproc_t *coproc, wifi_network_t *networks,
				uint16_t *count);

	/* Connection */
	int (*join_network)(wifi_coproc_t *coproc, const wifi_join_params_t *params);
	int (*leave_network)(wifi_coproc_t *coproc);
	int (*get_connected_network)(wifi_coproc_t *coproc, wifi_network_t *network);

	/* Packet I/O */
	int (*send_packet)(wifi_coproc_t *coproc, const uint8_t *data, size_t len);
	int (*receive_packet)(wifi_coproc_t *coproc, uint8_t *buffer, size_t buf_len,
			      size_t *out_len);

	/* Power management */
	int (*set_power_save)(wifi_coproc_t *coproc, bool enabled);

	/* Polling/async notification */
	int (*poll)(wifi_coproc_t *coproc);

} wifi_coproc_ops_t;

typedef struct wifi_coproc {
	char name[WIFI_COPROC_NAME_MAX];
	wifi_coproc_status_t status;
	void *transport_data;           /* UART, SPI, USB context */
	void *driver_data;              /* Chip-specific state */

	const wifi_coproc_ops_t *ops;

	/* Backing netdev */
	netdev_t *netdev;

	/* Statistics */
	uint64_t frames_tx;
	uint64_t frames_rx;
	uint64_t errors;
} wifi_coproc_t;

/* Public API */
int wifi_coproc_create(const char *name, wifi_coproc_t **out_coproc);
int wifi_coproc_destroy(wifi_coproc_t *coproc);

int wifi_coproc_register_ops(wifi_coproc_t *coproc, const wifi_coproc_ops_t *ops);
int wifi_coproc_register_transport(wifi_coproc_t *coproc, void *transport_data);

/* Lifecycle */
int wifi_coproc_init(wifi_coproc_t *coproc);
int wifi_coproc_deinit(wifi_coproc_t *coproc);

/* Network operations */
int wifi_coproc_scan(wifi_coproc_t *coproc);
int wifi_coproc_join(wifi_coproc_t *coproc, const char *ssid, const char *password,
		     wifi_auth_mode_t auth);
int wifi_coproc_disconnect(wifi_coproc_t *coproc);

/* Status queries */
wifi_coproc_status_t wifi_coproc_get_status(wifi_coproc_t *coproc);
const char *wifi_coproc_status_str(wifi_coproc_status_t status);

#endif /* KERNEL_DRIVERS_WIFI_COPROCESSOR_H */

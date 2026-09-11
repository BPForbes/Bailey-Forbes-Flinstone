/*
 * WiFi Supplicant for WPA/WPA2/WPA3 Authentication
 * Handles 4-way handshake and SAE (Simultaneous Authentication of Equals)
 * Phase 3 of v4.3.0 WiFi drivers roadmap
 * Supports IEEE 802.11i and RFC 7664 (WPA3-SAE)
 */

#ifndef KERNEL_DRIVERS_WIFI_SUPPLICANT_H
#define KERNEL_DRIVERS_WIFI_SUPPLICANT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "wifi_coprocessor.h"

#include "kernel/core/net/net_wifi_sae.h"

#define WIFI_SUPPLICANT_TIMEOUT_MS 10000

typedef enum {
	WIFI_SUPP_STATE_IDLE,
	WIFI_SUPP_STATE_AUTHENTICATING,
	WIFI_SUPP_STATE_4WAY_HANDSHAKE,
	WIFI_SUPP_STATE_GROUP_HANDSHAKE,
	WIFI_SUPP_STATE_AUTHENTICATED,
	WIFI_SUPP_STATE_ERROR
} wifi_supplicant_state_t;

typedef struct {
	uint8_t bssid[6];
	uint8_t pmk[32];	 /* Pairwise Master Key */
	uint8_t ptk[64];	 /* Pairwise Transient Key (full) */
	uint8_t gtk[32];	 /* Group Transient Key */
	uint32_t replay_counter; /* Anti-replay counter for GTK */
} wifi_supplicant_keys_t;

typedef struct {
	wifi_supplicant_state_t state;
	wifi_supplicant_keys_t keys;
	uint8_t sta_addr[6];

	/* Handshake state */
	uint32_t msg_num;	/* Current message in 4-way handshake (1-4) */
	uint32_t replay_ctr; /* Replay counter for 4-way handshake */

	/* Timeout tracking */
	uint32_t start_time_ms;
	uint32_t last_update_ms;

	/* Error tracking */
	uint32_t handshake_errors;

	/* Credentials for SAE / PSK derivation (ephemeral; scrub on deinit) */
	char ssid[WIFI_SSID_MAX + 1];
	char password[WIFI_PASSWORD_MAX + 1];

	/** RFC 7664 Dragonfly SAE state (WPA3); NULL when idle. */
	fl_net_wifi_sae_dragonfly_ctx_t *sae_dragonfly;
} wifi_supplicant_t;

/* Lifecycle */
int wifi_supplicant_init(wifi_supplicant_t *supp, const uint8_t *bssid);
int wifi_supplicant_set_credentials(wifi_supplicant_t *supp, const char *ssid,
				    const char *password);
int wifi_supplicant_set_sta_addr(wifi_supplicant_t *supp, const uint8_t sta_addr[6]);
int wifi_supplicant_deinit(wifi_supplicant_t *supp);

/* Key derivation (leverages existing net_wifi_crypto.c) */
int wifi_supplicant_derive_pmk_psk(wifi_supplicant_t *supp, const char *ssid,
				   const char *password);
int wifi_supplicant_derive_ptk(wifi_supplicant_t *supp, const uint8_t *anonce,
			       const uint8_t *snonce, const uint8_t *client_addr);
int wifi_supplicant_derive_gtk(wifi_supplicant_t *supp, const uint8_t *gtk_encr);

/* 4-way handshake */
int wifi_supplicant_start_4way_handshake(wifi_supplicant_t *supp);
int wifi_supplicant_process_msg1(wifi_supplicant_t *supp, const uint8_t *msg1,
				 size_t len);
int wifi_supplicant_process_msg3(wifi_supplicant_t *supp, const uint8_t *msg3,
				 size_t len);

/* WPA3-SAE (RFC 7664 Dragonfly group 19) */
int wifi_supplicant_start_sae_handshake(wifi_supplicant_t *supp);
int wifi_supplicant_build_sae_commit(wifi_supplicant_t *supp, const uint8_t *anticlogging_token,
				     size_t anticlogging_len, uint8_t *body, size_t body_cap,
				     size_t *body_len_out);
int wifi_supplicant_rx_sae_peer_commit(wifi_supplicant_t *supp, const uint8_t *commit, size_t len);
int wifi_supplicant_build_sae_confirm(wifi_supplicant_t *supp, uint8_t *body, size_t body_cap,
				      size_t *body_len_out);
int wifi_supplicant_process_sae_commit(wifi_supplicant_t *supp, const uint8_t *commit,
				       size_t len);
int wifi_supplicant_process_sae_confirm(wifi_supplicant_t *supp, const uint8_t *confirm,
					size_t len);

/* State management */
wifi_supplicant_state_t wifi_supplicant_get_state(wifi_supplicant_t *supp);
const char *wifi_supplicant_state_str(wifi_supplicant_state_t state);

/* Key installation */
int wifi_supplicant_install_ptk(wifi_supplicant_t *supp, wifi_coproc_t *coproc);
int wifi_supplicant_install_gtk(wifi_supplicant_t *supp, wifi_coproc_t *coproc);

#endif /* KERNEL_DRIVERS_WIFI_SUPPLICANT_H */

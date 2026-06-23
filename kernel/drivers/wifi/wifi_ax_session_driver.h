#ifndef KERNEL_DRIVERS_WIFI_WIFI_AX_SESSION_DRIVER_H
#define KERNEL_DRIVERS_WIFI_WIFI_AX_SESSION_DRIVER_H

#include "contract_p3_session_wire.h"
#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "net_wifi_he.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Session-wire WiFi L2 execution for 802.11ax OTA emulation (#279).
 * net_wifi_ax_server orchestrates P3 server/client I/O; this module runs
 * supplicant state and builds mgmt/EAPOL payloads.
 */

typedef struct wifi_ax_session_io {
    void *ctx;
    fl_result_t (*send)(void *ctx, uint8_t opcode, const uint8_t *payload, uint16_t plen);
    fl_result_t (*recv)(void *ctx, uint8_t expect_opcode, uint8_t *payload, uint16_t cap,
                        uint16_t *plen_out, unsigned timeout_ms);
} wifi_ax_session_io_t;

fl_result_t wifi_ax_ap_sae_confirm(uint8_t *confirm_out, size_t confirm_cap,
				   size_t *confirm_len_out);

fl_result_t wifi_ax_ap_eapol_responses(uint8_t *msg1_out, uint16_t *msg1_len_out,
				       uint8_t *msg3_out, uint16_t *msg3_len_out);

fl_result_t wifi_ax_ap_assoc_response(const uint8_t ap_bssid[6], const uint8_t *assoc_req,
				      uint16_t assoc_req_len, uint8_t *resp_out,
				      size_t resp_cap, size_t *resp_len_out);

fl_result_t wifi_ax_station_session_auth(const fl_net_wifi_cred_t *cred, uint8_t auth_mode,
					 const uint8_t sta_mac[6], const uint8_t ap_bssid[6],
					 const wifi_ax_session_io_t *io, unsigned timeout_ms,
					 uint8_t *assoc_resp_out, uint16_t assoc_resp_cap,
					 uint16_t *assoc_resp_len_out);

fl_result_t wifi_ax_station_parse_he_cap(const uint8_t *assoc_resp, uint16_t assoc_resp_len,
					 fl_net_wifi_he_cap_t *he_cap_out);

#endif /* KERNEL_DRIVERS_WIFI_WIFI_AX_SESSION_DRIVER_H */

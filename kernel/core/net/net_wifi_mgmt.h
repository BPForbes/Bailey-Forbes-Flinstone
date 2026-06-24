#ifndef NET_WIFI_MGMT_H
#define NET_WIFI_MGMT_H

#include "contract_p3_wifi.h"

#include <stddef.h>
#include <stdint.h>

#define FL_WIFI_MGMT_HDR_LEN 24u
#define FL_WIFI_ELEM_SSID 0u

int fl_net_wifi_mgmt_hdr_valid(const uint8_t *frame, size_t len);

/** Build a minimal Probe Request (24-byte hdr + SSID IE). */
fl_result_t fl_net_wifi_mgmt_build_probe_req(const char *ssid, uint8_t *out, size_t out_cap,
                                             size_t *out_len);

/**
 * Parse Probe Response / Beacon fixed fields + IE blob.
 * **ies_out** points into **frame**; valid while **frame** is alive.
 */
fl_result_t fl_net_wifi_mgmt_parse_mgmt_ies(const uint8_t *frame, size_t len,
                                            const uint8_t **ies_out, size_t *ies_len);

/** Build Association Request (header + SSID + optional RSNE + HE cap stub). */
fl_result_t fl_net_wifi_mgmt_build_assoc_req(const char *ssid, const uint8_t bssid[6],
                                             const uint8_t sta_mac[6], uint8_t auth_mode,
                                             uint8_t *out, size_t out_cap, size_t *out_len);

/** Build Open-System Authentication Request (802.11 management). */
fl_result_t fl_net_wifi_mgmt_build_auth_req(const uint8_t sta_mac[6], const uint8_t bssid[6],
                                            uint8_t *out, size_t out_cap, size_t *out_len);

/** Build Open-System Authentication Response (status 0). */
fl_result_t fl_net_wifi_mgmt_build_auth_resp(const uint8_t bssid[6], const uint8_t sta_mac[6],
                                             uint16_t auth_seq, uint8_t *out, size_t out_cap,
                                             size_t *out_len);

/** Build WPA3-SAE Authentication frame (algorithm 3) with optional body. */
fl_result_t fl_net_wifi_mgmt_build_sae_auth(const uint8_t sta_mac[6], const uint8_t bssid[6],
                                            uint16_t auth_seq, const uint8_t *body, size_t body_len,
                                            uint8_t *out, size_t out_cap, size_t *out_len);

/** Build RSNE (WPA2/WPA3) information element bytes. */
fl_result_t fl_net_wifi_mgmt_build_rsne_ie(uint8_t auth_mode, uint8_t *out, size_t out_cap,
                                           size_t *out_len);

/** Build Association Response with HE Capabilities + HE Operation IEs. */
fl_result_t fl_net_wifi_mgmt_build_assoc_resp(const uint8_t bssid[6], const uint8_t sta_mac[6],
                                              uint8_t *out, size_t out_cap, size_t *out_len);

#endif /* NET_WIFI_MGMT_H */

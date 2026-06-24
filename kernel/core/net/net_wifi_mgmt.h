#ifndef NET_WIFI_MGMT_H
#define NET_WIFI_MGMT_H

#include "contract_p3_wifi.h"

#include <stddef.h>
#include <stdint.h>

#define FL_WIFI_MGMT_HDR_LEN 24u
#define FL_WIFI_ELEM_SSID 0u
#define FL_WIFI_ELEM_SUPPORTED_RATES 1u
#define FL_WIFI_ELEM_DS_PARAM 3u
#define FL_WIFI_ELEM_RSN 48u

#define FL_WIFI_MGMT_FC_ASSOC_REQ 0x0000u
#define FL_WIFI_MGMT_FC_ASSOC_RESP 0x0010u
#define FL_WIFI_MGMT_FC_PROBE_REQ 0x0040u
#define FL_WIFI_MGMT_FC_PROBE_RESP 0x0050u
#define FL_WIFI_MGMT_FC_BEACON 0x0080u
#define FL_WIFI_MGMT_FC_AUTH 0x00b0u

#define FL_WIFI_AUTH_ALG_OPEN 0u
#define FL_WIFI_AUTH_ALG_SAE 3u

#define FL_WIFI_SCAN_SSID_MAX_LEN 32u

int fl_net_wifi_mgmt_hdr_valid(const uint8_t *frame, size_t len);

/** Validate directed-scan SSID length (1–32 bytes); **len_out** optional. */
fl_result_t fl_net_wifi_scan_ssid_validate(const char *ssid, size_t *len_out);

/** Build Probe Request (24-byte hdr + SSID IE) with **sta_mac** as Addr2. */
fl_result_t fl_net_wifi_mgmt_build_probe_req(const char *ssid, const uint8_t sta_mac[6],
                                             uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * Parse Probe Response / Beacon fixed fields + IE blob.
 * **ies_out** points into **frame**; valid while **frame** is alive.
 */
fl_result_t fl_net_wifi_mgmt_parse_mgmt_ies(const uint8_t *frame, size_t len,
                                            const uint8_t **ies_out, size_t *ies_len);

/** Parsed BSS fields from Probe Response / Beacon management frames. */
typedef struct {
    char ssid[FL_WIFI_SCAN_SSID_MAX];
    uint8_t bssid[FL_WIFI_BSSID_LEN];
    uint8_t supported_rates[12];
    uint8_t supported_rates_len;
    uint8_t channel;
    uint8_t auth_mode;
    uint64_t beacon_timestamp;
    uint16_t beacon_interval;
    uint16_t capability;
    uint16_t aid;
    uint16_t status_code;
    uint8_t he_supported;
    uint8_t bss_color;
    uint8_t channel_width_mhz;
    uint8_t twt_responder;
    uint8_t max_nss_rx;
    uint8_t max_nss_tx;
} fl_net_wifi_mgmt_bss_info_t;

fl_result_t fl_net_wifi_mgmt_parse_probe_resp(const uint8_t *frame, size_t len,
                                              fl_net_wifi_mgmt_bss_info_t *out);

fl_result_t fl_net_wifi_mgmt_parse_beacon(const uint8_t *frame, size_t len,
                                          fl_net_wifi_mgmt_bss_info_t *out);

fl_result_t fl_net_wifi_mgmt_parse_auth_resp(const uint8_t *frame, size_t len,
                                             uint16_t *auth_alg_out, uint16_t *auth_seq_out,
                                             uint16_t *status_out, const uint8_t **body_out,
                                             size_t *body_len_out);

fl_result_t fl_net_wifi_mgmt_parse_assoc_resp(const uint8_t *frame, size_t len,
                                              uint16_t *status_out, uint16_t *aid_out,
                                              fl_net_wifi_he_cap_t *he_out);

void fl_net_wifi_mgmt_bss_to_scan_entry(const fl_net_wifi_mgmt_bss_info_t *bss,
                                        fl_net_wifi_scan_entry_t *entry);

/** Merge **src** into **dst** (beacon + probe response for the same BSSID). */
void fl_net_wifi_scan_entry_merge(fl_net_wifi_scan_entry_t *dst,
                                  const fl_net_wifi_scan_entry_t *src);

/** Parse SSID + HE fields from a scan IE blob into **entry**. */
fl_result_t fl_net_wifi_mgmt_enrich_scan_from_ies(const uint8_t *ies, size_t ies_len,
                                                  fl_net_wifi_scan_entry_t *entry);

/** Build HE Capabilities extension IE from station PHY capabilities. */
fl_result_t fl_net_wifi_mgmt_build_he_cap_ie(const fl_net_wifi_he_cap_t *sta_he, uint8_t *out,
                                             size_t out_cap, size_t *out_len);

/** Build Association Request (header + SSID + RSNE + HE Cap from **sta_he**). */
fl_result_t fl_net_wifi_mgmt_build_assoc_req(const char *ssid, const uint8_t bssid[6],
                                             const uint8_t sta_mac[6], uint8_t auth_mode,
                                             const fl_net_wifi_he_cap_t *sta_he, uint8_t *out,
                                             size_t out_cap, size_t *out_len);

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

/** SAE commit (algorithm 3, sequence 1). */
fl_result_t fl_net_wifi_mgmt_build_auth_sae_commit(const uint8_t sta_mac[6],
                                                   const uint8_t bssid[6], const uint8_t *commit,
                                                   size_t commit_len, uint8_t *out, size_t out_cap,
                                                   size_t *out_len);

/** SAE confirm (algorithm 3, sequence 2). */
fl_result_t fl_net_wifi_mgmt_build_auth_sae_confirm(const uint8_t sta_mac[6],
                                                    const uint8_t bssid[6],
                                                    const uint8_t *confirm, size_t confirm_len,
                                                    uint8_t *out, size_t out_cap, size_t *out_len);

/** Build RSNE (WPA2/WPA3) information element bytes. */
fl_result_t fl_net_wifi_mgmt_build_rsne_ie(uint8_t auth_mode, uint8_t *out, size_t out_cap,
                                           size_t *out_len);

/** Build Association Response with HE Capabilities + HE Operation IEs. */
fl_result_t fl_net_wifi_mgmt_build_assoc_resp(const uint8_t bssid[6], const uint8_t sta_mac[6],
                                              uint8_t *out, size_t out_cap, size_t *out_len);

#endif /* NET_WIFI_MGMT_H */

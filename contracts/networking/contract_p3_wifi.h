/**
 * **P3-10 — Wi-Fi 802.11ax station path** (module contract, normative).
 *
 * Promoted for **#279** / **#328** (P3-10 / P4-01). L2 data still crosses
 * **fl_net_frame_view_t** (same pipeline as Ethernet); upper stack uses
 * **fl_net_driver_t** + **P3-5** / **P3-6** / **P3-7** unchanged.
 *
 * **Integration status:** contract + parsers + lab/mock/host-Linux HE enrichment;
 * in-tree FullMAC RF bring-up returns **FL_RESULT_NOSYS** until chipset USB/PCI
 * command rings land (host **FL_NET_WIFI_USE_WPA=1** path is production RF today).
 *
 * Standards (informative): IEEE 802.11ax-2021 / 802.11-2020 HE IEs; WPA3-SAE;
 * WPA2-PSK backward-compat; **RFC 2131** DHCP after link-up (**P3-12**).
 */
#ifndef FL_CONTRACT_P3_WIFI_H
#define FL_CONTRACT_P3_WIFI_H

#include "contract_p3_wire.h"

#include <stdint.h>

#define FL_CONTRACT_P3_10_WIFI_CONTRACT_DEFINED 1
#define FL_CONTRACT_P3_10_WIFI_REV 1u

/** IEEE 802.11 element IDs (subset). */
#define FL_WIFI_ELEM_ID_EXTENSION 255u
#define FL_WIFI_EXT_HE_CAPABILITIES 35u
#define FL_WIFI_EXT_HE_OPERATION 36u

#define FL_WIFI_SSID_MAX 32u
#define FL_WIFI_PASSPHRASE_MAX 64u
#define FL_WIFI_BSSID_LEN 6u
#define FL_WIFI_SCAN_SSID_MAX 33u

#define FL_WIFI_AUTH_OPEN 0x00u
#define FL_WIFI_AUTH_OWE 0x01u
#define FL_WIFI_AUTH_WPA2_PSK 0x02u
#define FL_WIFI_AUTH_WPA3_SAE 0x03u
#define FL_WIFI_AUTH_8021X 0x04u

#define FL_WIFI_BAND_ANY 0x00u
#define FL_WIFI_BAND_2GHZ 0x01u
#define FL_WIFI_BAND_5GHZ 0x02u
#define FL_WIFI_BAND_6GHZ 0x03u

typedef enum {
    FL_WIFI_STATE_IDLE = 0,
    FL_WIFI_STATE_SCANNING = 1,
    FL_WIFI_STATE_AUTHING = 2,
    FL_WIFI_STATE_ASSOC = 3,
    FL_WIFI_STATE_CONNECTED = 4,
    FL_WIFI_STATE_DHCP = 5,
    FL_WIFI_STATE_UP = 6,
    FL_WIFI_STATE_ERROR = 7
} fl_net_wifi_state_t;

typedef struct {
    uint8_t max_nss_rx;
    uint8_t max_nss_tx;
    uint8_t supports_ofdma;
    uint8_t supports_mu_mimo;
    uint8_t supports_twt;
    uint8_t bss_color;
    uint8_t channel_width_mhz;
    uint8_t supports_6ghz;
} fl_net_wifi_he_cap_t;

typedef struct {
    uint64_t twt_target_us;
    uint32_t wake_duration_us;
    uint32_t wake_interval_us;
    uint8_t flow_id;
    uint8_t implicit;
    uint8_t announced;
    uint8_t trigger_enabled;
} fl_net_wifi_twt_params_t;

/**
 * Credential record for association. Passphrase bytes are ephemeral on the wire
 * path only — long-term secret storage belongs to **P2/P5**, not this struct.
 */
typedef struct {
    char ssid[FL_WIFI_SSID_MAX];
    char passphrase[FL_WIFI_PASSPHRASE_MAX];
    uint8_t bssid[FL_WIFI_BSSID_LEN];
    uint8_t auth_mode;
    uint8_t band_hint;
} fl_net_wifi_cred_t;

typedef struct {
    char ssid[FL_WIFI_SCAN_SSID_MAX];
    uint8_t bssid[FL_WIFI_BSSID_LEN];
    int8_t rssi_dbm;
    uint8_t channel;
    uint8_t auth_mode;
    uint8_t band;
    uint8_t channel_width_mhz;
    uint8_t he_supported;
    uint8_t bss_color;
    uint8_t twt_responder;
    int8_t rssi_5ghz_dbm;
} fl_net_wifi_scan_entry_t;

#endif /* FL_CONTRACT_P3_WIFI_H */

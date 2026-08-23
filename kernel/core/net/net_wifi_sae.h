#ifndef NET_WIFI_SAE_H
#define NET_WIFI_SAE_H

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "net_wifi_crypto.h"

#include <stddef.h>
#include <stdint.h>

/** IEEE 802.11 SAE group 19 — NIST P-256 (RFC 7664 Dragonfly ECC). */
#define FL_NET_WIFI_SAE_GROUP_19 19u

/** Commit body: group (2) + scalar (32) + element x||y (64) for group 19. */
#define FL_NET_WIFI_SAE_COMMIT_BODY_LEN 98u
#define FL_NET_WIFI_SAE_SCALAR_WIRE_LEN 32u
#define FL_NET_WIFI_SAE_ELEMENT_WIRE_LEN 64u

/** Confirm body: send-confirm (2) + HMAC-SHA256 (32). */
#define FL_NET_WIFI_SAE_CONFIRM_BODY_LEN 34u

/** Lab / vector PMK fingerprint from SAE KDF (password + SSID context). */
fl_result_t fl_net_wifi_sae_derive_pmk(const char *ssid, const char *passphrase,
                                       uint8_t *pmk_out, size_t pmk_cap);

/** RFC 7664 / 802.11-2020 SAE KDF self-test (hosted CI). */
fl_result_t fl_net_wifi_sae_rfc7664_kdf_selftest(void);

/**
 * Opaque Dragonfly (RFC 7664) SAE exchange context for group 19.
 * Hunt-and-peck PWE, commit/confirm, PMK + KCK derivation.
 */
typedef struct fl_net_wifi_sae_dragonfly_ctx fl_net_wifi_sae_dragonfly_ctx_t;

/** Allocate Dragonfly state (OpenSSL EC). Caller must call deinit. */
fl_result_t fl_net_wifi_sae_dragonfly_ctx_create(fl_net_wifi_sae_dragonfly_ctx_t **ctx_out);

void fl_net_wifi_sae_dragonfly_ctx_destroy(fl_net_wifi_sae_dragonfly_ctx_t *ctx);

/** Initialize as STA (supplicant) for WPA3-SAE. */
fl_result_t fl_net_wifi_sae_dragonfly_init_sta(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                               const char *ssid, const char *password,
                                               const uint8_t sta_mac[6],
                                               const uint8_t bssid[6]);

/** Initialize as AP (authenticator) for mock / hwsim. */
fl_result_t fl_net_wifi_sae_dragonfly_init_ap(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                              const char *ssid, const char *password,
                                              const uint8_t ap_bssid[6],
                                              const uint8_t sta_mac[6]);

/**
 * Build SAE Commit body (group 19). Optional anti-clogging token is placed
 * immediately after the group identifier and before the scalar (802.11-2020
 * 12.4.7.4).
 */
fl_result_t fl_net_wifi_sae_dragonfly_build_commit(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                   const uint8_t *anticlogging_token,
                                                   size_t anticlogging_len, uint8_t *body,
                                                   size_t body_cap, size_t *body_len_out);

/** Parse and process peer Commit; derive KCK/PMK (peer commit must be complete). */
fl_result_t fl_net_wifi_sae_dragonfly_rx_commit(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                const uint8_t *body, size_t body_len);

/** Build SAE Confirm body after peer Commit was processed. */
fl_result_t fl_net_wifi_sae_dragonfly_build_confirm(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                    uint8_t *body, size_t body_cap,
                                                    size_t *body_len_out);

/** Verify peer Confirm and output PMK. */
fl_result_t fl_net_wifi_sae_dragonfly_verify_confirm(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                     const uint8_t *body, size_t body_len,
                                                     uint8_t pmk_out[FL_NET_WIFI_PMK_LEN]);

/** Round-trip STA+AP Dragonfly exchange self-test (RFC 7664 group 19). */
fl_result_t fl_net_wifi_sae_dragonfly_selftest(void);

#endif /* NET_WIFI_SAE_H */

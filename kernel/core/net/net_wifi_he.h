#ifndef NET_WIFI_HE_H
#define NET_WIFI_HE_H

#include "contract_p3_wifi.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Walk **802.11 Information Elements** (ID/length/value tuples) and locate an
 * **Extension** element with the given **Extension ID** (e.g. HE Capabilities).
 * Returns 1 and sets **body** / **body_len** to bytes after the Extension ID.
 */
int fl_net_wifi_ie_find_extension(const uint8_t *ies, size_t ies_len, uint8_t ext_id,
                                  const uint8_t **body, size_t *body_len);

/** Parse HE Capabilities extension body (after Extension ID byte). */
int fl_net_wifi_he_parse_capabilities(const uint8_t *body, size_t body_len,
                                      fl_net_wifi_he_cap_t *out);

/** Parse HE Operation extension body; optional **bss_color** / **twt_responder**. */
int fl_net_wifi_he_parse_operation(const uint8_t *body, size_t body_len,
                                   uint8_t *bss_color_out, uint8_t *twt_responder_out);

/** Enrich a scan entry from a concatenated IE blob (Beacon / Probe Response). */
int fl_net_wifi_scan_enrich_from_ies(const uint8_t *ies, size_t ies_len,
                                     fl_net_wifi_scan_entry_t *entry);

#endif /* NET_WIFI_HE_H */

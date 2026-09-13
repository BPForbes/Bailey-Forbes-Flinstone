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

/** Host-side HE hints when RF is delegated (nmcli / FlinstonePowershell / iw). */
void fl_net_wifi_host_he_hint(fl_net_wifi_scan_entry_t *entry, const char *flags,
                              const char *rate_mbit);

void fl_net_wifi_host_he_cap_from_entry(const fl_net_wifi_scan_entry_t *ap,
                                        fl_net_wifi_he_cap_t *cap_out);

#if defined(__linux__)

size_t fl_net_wifi_iw_enrich_scan(const char *iface, fl_net_wifi_scan_entry_t *entries,
                                  size_t count);

int fl_net_wifi_iw_connected_he_cap(const char *iface,
                                    const fl_net_wifi_scan_entry_t *join_ap,
                                    fl_net_wifi_he_cap_t *cap_out);

#else

static inline size_t fl_net_wifi_iw_enrich_scan(const char *iface,
                                                fl_net_wifi_scan_entry_t *entries,
                                                size_t count)
{
    (void)iface;
    (void)entries;
    (void)count;
    return 0u;
}

static inline int fl_net_wifi_iw_connected_he_cap(const char *iface,
                                                  const fl_net_wifi_scan_entry_t *join_ap,
                                                  fl_net_wifi_he_cap_t *cap_out)
{
    (void)iface;
    (void)join_ap;
    (void)cap_out;
    return -1;
}

#endif

#endif /* NET_WIFI_HE_H */

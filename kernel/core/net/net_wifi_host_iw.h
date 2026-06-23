#ifndef NET_WIFI_HOST_IW_H
#define NET_WIFI_HOST_IW_H

#include "contract_p3_wifi.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Host-side 802.11ax hints when RF is delegated to Linux (nmcli / wpa_cli / mt7921u).
 * Heuristics fill contract scan fields; optional `iw scan dump` enriches from HE IEs.
 */

/** Apply band / flags / rate heuristics to a scan row (in-place). */
void fl_net_wifi_host_he_hint(fl_net_wifi_scan_entry_t *entry, const char *flags,
                              const char *rate_mbit);

/** Derive negotiated **fl_net_wifi_he_cap_t** from a scan/join AP row. */
void fl_net_wifi_host_he_cap_from_entry(const fl_net_wifi_scan_entry_t *ap,
                                        fl_net_wifi_he_cap_t *cap_out);

#if defined(__linux__)

/** Match **entries** to `iw dev IFACE scan dump` BSS blocks; enrich HE IE fields. */
size_t fl_net_wifi_iw_enrich_scan(const char *iface, fl_net_wifi_scan_entry_t *entries,
                                  size_t count);

/**
 * HE capabilities for a connected interface. Uses **join_ap** when supplied;
 * otherwise best-effort from `iw dev IFACE link` + heuristics.
 * Returns 0 on success, -1 when iface is down or iw unavailable.
 */
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

#endif /* NET_WIFI_HOST_IW_H */

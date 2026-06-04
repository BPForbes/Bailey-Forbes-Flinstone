#ifndef NET_WIFI_HOST_LINUX_H
#define NET_WIFI_HOST_LINUX_H

#include "contract_p3_wifi.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Linux **P4** WLAN driver backend — **wpa_supplicant** control socket (**wpa_cli**).
 * This is the host driver boundary (cfg80211/mac80211 + supplicant); MLME/WPA in-tree
 * remain the lab path when no NIC is present.
 *
 * Env:
 *   **FL_NET_WIFI_IFACE** — interface name (default `wlan0`)
 *   **FL_NET_WIFI_WPA_CLI** — path to wpa_cli (default `wpa_cli`)
 *   **FL_NET_WIFI_USE_WPA=1** — force wpa path
 *   **FL_NET_WIFI_USE_WPA=0** — force in-tree lab simulation
 *   (unset) — auto: use wpa when `wpa_cli ping` succeeds
 */

int fl_net_wifi_host_linux_available(void);

fl_result_t fl_net_wifi_host_linux_scan(uint8_t band, unsigned timeout_ms);
fl_result_t fl_net_wifi_host_linux_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                               size_t *count_out);
fl_result_t fl_net_wifi_host_linux_connect(const fl_net_wifi_cred_t *cred,
                                           unsigned timeout_ms);
fl_result_t fl_net_wifi_host_linux_disconnect(void);

/** IPv4 on the WLAN interface after association (from OS routing stack). */
fl_result_t fl_net_wifi_host_linux_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len);

/** Prefix length and default gateway on the WLAN interface when known. */
fl_result_t fl_net_wifi_host_linux_ipv4_route(uint32_t *addr_be_out, uint8_t *prefix_len_out,
                                              uint32_t *gw_be_out);

const char *fl_net_wifi_host_linux_iface(void);

#endif /* NET_WIFI_HOST_LINUX_H */

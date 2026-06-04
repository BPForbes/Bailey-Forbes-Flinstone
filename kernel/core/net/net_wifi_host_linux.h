#ifndef NET_WIFI_HOST_LINUX_H
#define NET_WIFI_HOST_LINUX_H

#include "contract_p3_wifi.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Linux hosted WLAN via **wpa_supplicant** control (**wpa_cli**).
 * Requires `wpa_cli` on PATH and an interface managed by wpa_supplicant.
 *
 * Env:
 *   **FL_NET_WIFI_IFACE** — interface name (default `wlan0`)
 *   **FL_NET_WIFI_WPA_CLI** — path to wpa_cli (default `wpa_cli`)
 *   **FL_NET_WIFI_USE_WPA=1** — force wpa path (skip lab simulation)
 *   **FL_NET_WIFI_USE_WPA=0** — force lab simulation
 */

int fl_net_wifi_host_linux_available(void);

fl_result_t fl_net_wifi_host_linux_scan(uint8_t band, unsigned timeout_ms);
fl_result_t fl_net_wifi_host_linux_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                               size_t *count_out);
fl_result_t fl_net_wifi_host_linux_connect(const fl_net_wifi_cred_t *cred,
                                           unsigned timeout_ms);
fl_result_t fl_net_wifi_host_linux_disconnect(void);

/** IPv4 on the WLAN interface after association (0 when none). */
fl_result_t fl_net_wifi_host_linux_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len);

const char *fl_net_wifi_host_linux_iface(void);

#endif /* NET_WIFI_HOST_LINUX_H */

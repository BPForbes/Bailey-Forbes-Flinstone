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
 *   **FL_NET_WIFI_IFACE** — interface name (auto-detect first Wi-Fi netdev when unset)
 *   **FL_NET_WIFI_WPA_CLI** — path to wpa_cli (default `wpa_cli`)
 *   **FL_NET_WIFI_NMCLI** — path to nmcli (default `nmcli`)
 *   **FL_NET_WIFI_USE_WPA=1** — force wpa_cli path
 *   **FL_NET_WIFI_USE_WPA=0** — force in-tree lab simulation
 *   (unset) — auto: wpa_cli when `ping` succeeds, else NetworkManager **nmcli**
 */

int fl_net_wifi_host_linux_available(void);

/** `"wpa_cli"`, `"nmcli"`, or NULL when no host backend is active. */
const char *fl_net_wifi_host_linux_backend_name(void);

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

/** Global unicast IPv6 on the WLAN interface after router SLAAC/DHCPv6. */
fl_result_t fl_net_wifi_host_linux_ipv6(uint8_t addr6[16], char *buf, size_t buf_len);

/** IPv6 prefix length on the WLAN interface when known. */
fl_result_t fl_net_wifi_host_linux_ipv6_route(uint8_t addr6[16], uint8_t *prefix_len_out);

const char *fl_net_wifi_host_linux_iface(void);

/**
 * Windows Wi-Fi adapter IP from FlinstonePowershell (e.g. "192.168.1.235").
 * Not bindable in Linux; use for display and peer-connection hints only.
 * Returns NULL when not on the FlinstonePowershell backend or not connected.
 */
const char *fl_net_wifi_host_linux_windows_ipv4(void);

/**
 * Spawn FlinstonePowershell server-bridge <port> in the background.
 * The bridge relays LAN connections to 127.0.0.1:<port> (WSL2 loopback).
 * No Windows admin rights needed for ports >= 1024.
 * Returns 0 when spawned (optimistic), -1 on wrong backend or missing exe.
 */
int fl_net_wifi_host_linux_server_bridge(uint16_t port);

/**
 * Add a Windows portproxy rule so LAN peers can reach the server at the
 * Windows Wi-Fi IP.  Requires FlinstonePowershell.exe to have admin rights.
 * Returns 0 on success, -1 on failure or wrong backend.
 */
int fl_net_wifi_host_linux_server_proxy(const char *wsl_ip, uint16_t port);

/** Remove a portproxy rule added by fl_net_wifi_host_linux_server_proxy(). */
int fl_net_wifi_host_linux_server_proxy_del(uint16_t port);

#endif /* NET_WIFI_HOST_LINUX_H */

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
 * Env (host helpers are **opt-in**; default is in-tree driver + lab netdev):
 *   **FL_NET_WIFI_IFACE** — interface name (auto-detect first Wi-Fi netdev when unset)
 *   **FL_NET_WIFI_WPA_CLI** — path to wpa_cli (default `wpa_cli`)
 *   **FL_NET_WIFI_NMCLI** — path to nmcli (default `nmcli`)
 *   **FL_NET_WIFI_FLINSTONE_PS** — FlinstonePowershell.exe path (WSL / tests)
 *   **FL_NET_WIFI_FLINSTONE_LINUX** — native Linux helper path
 *   **FL_NET_WIFI_USE_WPA=1** — enable wpa_cli / nmcli / helper backends
 *   **FL_NET_WIFI_USE_WPA=0** — force in-tree lab simulation (default when unset)
 *   **FL_NET_WIFI_BRIDGE_TARGET** — bridge target IP (default 127.0.0.1)
 *   **FL_NET_WIFI_BRIDGE_PY** — optional path to tools/network_bridge.py
 */

/** 1 when WSL2 mirrored networking is active (eth0 has a 192.168.x.x LAN IP).
 *  When true, the server binds directly to the LAN IP — no relay needed. */
int fl_net_wifi_host_linux_wsl_mirrored(void);

/** 1 when an external host helper path was explicitly requested via env. */
int fl_net_wifi_host_linux_opted_in(void);

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
 * Spawn a server bridge bound to a Windows/LAN address and forwarding to
 * target_ip:port.  bind_ip may be NULL/empty for helper auto-detect/all-ifaces;
 * target_ip may be NULL/empty to use FL_NET_WIFI_BRIDGE_TARGET or 127.0.0.1.
 */
int fl_net_wifi_host_linux_server_bridge_to(const char *bind_ip,
                                           const char *target_ip,
                                           uint16_t port);

/**
 * WSL eth0 / Linux-bindable IPv4 (e.g. 172.27.x.x).  Fills buf on success.
 * Returns 0 on success, -1 when unavailable.
 */
int fl_net_wifi_host_linux_wsl_ipv4(char *buf, size_t buf_len);

#endif /* NET_WIFI_HOST_LINUX_H */

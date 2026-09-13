# WiFi drivers (`kernel/drivers/wifi/`)

Layout for v4.3.0 WiFi integration (#279, P3-10).

## Top level

| File | Role |
|------|------|
| `wifi_driver_backend.c` | Routes scan/connect to coprocessor, FullMAC HW, lab mock, or lab simulation |
| `wifi_lab_backend.c` | Hosted lab NIC + **software FullMAC mock** (`FL_WIFI_80211AX_MOCK=1`) |
| `wifi_lab_router.c` | Lab AP/router DB for virtual scan |
| `wifi_supplicant.c` | In-tree SAE / WPA2-PSK supplicant for lab and mock OTA |
| `wifi_coprocessor.c` | Phase 1 UART AT coprocessor |
| `wifi_uart_transport.c` | UART framing for coprocessor |
| `wifi_driver_packet.c` | P3 packet bridge for WiFi netdev |
| `wifi_platform_*.c` | Host vs ARM alloc / I/O |

## `fullmac/` — Phase 4 hardware path

| File | Role |
|------|------|
| `wifi_fullmac.h` | Public FullMAC types and ops |
| `wifi_fullmac_chipset.h` | VID:PID table API |
| `wifi_fullmac_core.c` | Netdev glue, chipset table, firmware load, connect |
| `wifi_fullmac_bus.c` | PCIe + USB sysfs probe |
| `wifi_fullmac_hw.c` | Attach, stub ops, OS-bound HE when kernel driver owns chip |

Production RF on the dongle still requires chipset-specific firmware/USB rings; until then use **`FL_NET_WIFI_USE_WPA=1`** for real scan/join via the host OS.

## Related net layer (`kernel/core/net/`)

- `net_wifi_station.c` — FSM orchestration
- `net_wifi_host_linux.c` — nmcli / wpa_cli / FlinstonePowershell + host HE hints / optional `iw`
- `net_wifi_he.c` — HE IE parse (shared by lab, mock, host, session OTA)
- `net_wifi_ax_server.c` — P3 server host/join WiFi L2 over session wire

## Environment quick reference

```bash
# Real Wi-Fi via host OS
export FL_NET_WIFI_USE_WPA=1
export FL_NET_WIFI_IFACE=wlan0

# Software ax NIC (no RF)
export FL_WIFI_80211AX_MOCK=1

# FullMAC probe (MT7921AU etc.)
export FL_WIFI_FULLMAC=1
export FL_WIFI_FULLMAC_USB=2-1
```

Tests: `make test_wifi`, `make test_wifi_80211ax_mock_279`, `make test_wifi_fullmac_probe`, `make test_p3_wifi`, `make test_wifi_uart_at_scan_join`, `make test_wifi_connect_ota`, `make test_p3_wifi_ota` (physical; skips without `FL_NET_WIFI_IFACE` + `SSID`).

**#328** / **P4-01** is **closed**. Issue-specific `validate_issue_328.sh` and the GH Actions `wifi-issue-328` job are sunset. Physical OTA still uses an nl80211 STA from `iw dev` (`FL_NET_WIFI_IFACE`); Windows Wi-Fi is not `wlan0` inside WSL.


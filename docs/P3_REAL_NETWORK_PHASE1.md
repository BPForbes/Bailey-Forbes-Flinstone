# P3 real network — Phase 1 (TAP + DHCP)

Phase 1 connects the in-tree shell to a **real LAN IPv4 address** using the existing **RFC 2131** DHCP client (`kernel/core/net/net_dhcp.c`) over a **Linux TAP** device (`kernel/core/net/net_tap.c`). Wi‑Fi station join (#279) and live IPv6 from the router (#280 tail) are **later phases**.

## What works in-tree

| Piece | Role |
|-------|------|
| `fl_net_netdev_tap_open` | Creates TAP, reads **hardware MAC** from the kernel |
| `FL_NET_TAP_DHCP=1` | Skips static `10.0.2.15/24`; installs **0.0.0.0/0** with **source 0.0.0.0** for Discover/Request |
| `fl_net_dhcp_acquire_on_tap` | DISCOVER → OFFER → REQUEST → ACK; parses options **1** (mask) and **3** (router) when present |
| `dhcp acquire` | Shell verb (`userland/command/cmd_net_tools.c`) |

Default lab static TAP (`FL_NET_TAP_IPV4`, `FL_NET_TAP_PREFIX`, `FL_NET_TAP_GW`) is unchanged when **`FL_NET_TAP_DHCP` is unset**.

## Host setup (Linux)

The shell process does **not** create the bridge; you attach TAP on the host once per session.

1. Build and run the shell on a Linux host with `/dev/net/tun` and permission to open it (often `sudo` or membership in a group that can open TUN).

2. Open TAP from the shell (optional name hint):

   ```text
   dhcp acquire
   ```

   or `dhcp acquire -t 8000 fl0` to set timeout and interface name hint.

3. On the **host**, bridge the TAP interface to your LAN (example: Wi‑Fi `wlan0`, home `192.168.1.0/24`):

   ```bash
   TAP=fl0   # use the name printed by ifconfig / dhcp output
   sudo ip link set "$TAP" up
   sudo ip addr flush dev "$TAP"
   BR=fl-br0
   sudo ip link add name "$BR" type bridge
   sudo ip link set "$BR" up
   sudo ip link set "$TAP" master "$BR"
   sudo ip link set wlan0 master "$BR"   # or eth0
   sudo ip link set wlan0 up
   ```

   Exact commands vary by distro; some setups use `nmcli` or NetworkManager tap profiles instead.

4. Run **`dhcp acquire`** again (or once after the bridge carries broadcast DHCP). The client uses the TAP **MAC** on the wire; the lease should be a normal home-router address (e.g. `192.168.1.x`).

5. Verify:

   ```text
   route
   ping 192.168.1.1
   ```

## Environment variables

| Variable | Meaning |
|----------|---------|
| `FL_NET_TAP_DHCP=1` | Set automatically during `dhcp acquire`; use static TAP env vars when unset |
| `FL_NET_TAP_IPV4` / `FL_NET_TAP_PREFIX` / `FL_NET_TAP_GW` | Static TAP defaults (`10.0.2.15/24`, gw `10.0.2.2`) when not using DHCP |
| `FL_CONTRACT_P0_CI_SKIP_TAP=1` | CI skip (TAP unavailable) |

## Roadmap (not Phase 1)

- **Phase 2 (#279):** real Wi‑Fi station / `wpa_supplicant` or nl80211 integration.
- **Phase 3 (#280):** SLAAC / DHCPv6 / NDP on the live interface after IPv4 home join works.

See also `docs/P3_NETWORKING.md` and `contracts/networking/contract_p3_wifi_deferred.h`.

# `kernel/core/net` — P3 implementation

Long-form guide: **`docs/P3_NETWORKING.md`**. **P3-13 chat room plan:** **`docs/P3_13_CHAT_SERVER.md`**.

## Quick file index

| File | Purpose |
|------|---------|
| `net_wire.c` | L2/L3 frame vocabulary (`contract_p3_wire.h`) |
| `net_packet.c` | Layered **fl_net_packet_t** parse + RX/TX pipeline; **`fl_net_packet_bind_l4`** / **`l4_view`** for UDP/DHCP payloads (`contract_p3_packet.h`) |
| `net_dhcp.c` | P3-12 DHCP codec; BOOTP over **fl_net_packet_t** L4 slices |
| `net_arp.c` | P3-4 ARP cache (**asm_net_arp_cache_***), request/reply, resolve |
| `net_route.c` | P3-5 routing table |
| `net_wire_egress.c` | IPv4 L4 egress (ARP + netdev); `fl_net_wire_egress_l4_xmit` |
| `net_udp.c` | UDP datagram build + port demux RX queues |
| `net_socket.c` | **P3-13a** hosted socket shim (`fl_net_sock_*`) |
| `net_ipv4.c` | IPv4 header construction |
| `net_checksum.c` | Checksum (+ `asm_net_checksum16`) |
| `net_icmp.c` | ICMP echo |
| `net_tcp.c` | TCP SYN build/probe |
| `net_dns.c` | Resolve + DNS A |
| `net_loopback.c` | P3-2 software netdev |
| `net_netdev.c` | P3-1 driver registry |
| `net_tap.c` | P3-3 Linux TAP |
| `net_wire_host.c` | Hosted wire I/O (sport bind, loopback exchange) |
| `net_wire_host_syscall.c` | Linux socket syscall bridge |
| `net_ping_host.c` | `fl_net_ping` |
| `net_requirements.c` | CI probe report |
| `net_background.c` | **P3-14** workqueue tick; **task backend** socket four-tuple + ARP xmit, client→server→clients relay (P3-13 TODO for wire RX / shell) |

**Planned (P3-13 — see `docs/P3_13_CHAT_SERVER.md`):** `net_socket.c`, `net_server.c`, TCP FSM in `net_tcp.c` (P3-7); shell `cmd_server.c`.

## Includes

- Contracts: **`-Icontracts/networking`**
- ASM API: **`#include "fl/net_asm.h"`**
- Wire host syscalls: **`net_wire_host_syscall.h`**

## Build

Listed in **`NET_CORE_SRCS`** in the root **`Makefile`**, plus **`$(NET_ASM_OBJ)`** from **`arch/*/net_asm.*`** and **`net_wire_host_asm.*`**.

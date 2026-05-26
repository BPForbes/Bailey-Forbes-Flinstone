P3 (*contracts/networking*) — roadmap **P3-1** … **P3-12** (netdev, loopback,
TAP, ARP, IPv4, UDP, DHCP, TCP, DNS, TLS hosted, Wi‑Fi deferral, IPv6 deferral).

**Not a clone of P2:** **P3** freezes **octet paths, protocol headers, queues, and clocks**
on the network. **P2** still owns **principals, credentials, elevation, and the full authz
middleware** story. This tree composes **only** **P2-3** through **contract_p3_trust.h**
(`fl_authz_operation_t` / **FL_AUTHZ_OP_NETDEV_***) so TAP and promiscuous paths can call
the same gate without importing **contract_identity.h** in every shard. When you need
**FL_PRINCIPAL_*** or credential stores, `#include "contract_identity.h"` explicitly in the
same `.c` file.

**Umbrella header:** *contract_networking.h* — includes **contract_extend.h** (P0–P1),
**contract_p3_wire.h** (shared **P3** typedefs and MTU/ethertype constants),
**contract_p3_trust.h** (narrow **P2-3** include), **FL_CONTRACT_P3_NETWORKING_REV**, all
shards below, and **FL_CONTRACT_P3_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P3_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
| *contract_p3_wire.h* | Shared wire vocabulary (not a **P3-**`*` row) |
| *contract_p3_packet.h* | Layered packet + pipeline stages (cross-cutting; not a **P3-**`*` row) |
| *contract_p3_socket.h* | Socket four-tuple endpoint (cross-cutting; full **P3-13** shim TODO) |
| *contract_p3_trust.h* | Narrow **P2-3** include (not a **P3-**`*` row) |
| *contract_p3_netdev.h* | P3-1 |
| *contract_p3_loopback.h* | P3-2 |
| *contract_p3_tap.h* | P3-3 |
| *contract_p3_arp.h* | P3-4 |
| *contract_p3_ipv4.h* | P3-5 |
| *contract_p3_udp.h* | P3-6 |
| *contract_p3_dhcp.h* | P3-12 |
| *contract_p3_tcp.h* | P3-7 |
| *contract_p3_dns.h* | P3-8 |
| *contract_p3_tls_hosted.h* | P3-9 |
| *contract_p3_wifi_deferred.h* | P3-10 |
| *contract_p3_ipv6_deferred.h* | P3-11 |
| *contract_p3_background.h* | P3-14 |

Most shards include **contract_p3_wire.h** (which pulls **contract_extend.h**). **P3-1** and
**P3-3** also include **contract_p3_trust.h** for netdev **authz** op references.

**Build:** add **-Icontracts/networking** next to **-Icontracts/identity** in the root
**Makefile** **CFLAGS** and in **CMakeLists.txt** include directories for targets that compile
networking or **fl/driver/net.h**.

**Driver API:** **kernel/include/fl/driver/net.h** uses **fl_net_frame_view_t** /
**fl_net_frame_mut_t** and **fl_result_t** for **P3-1** interchange.

**Implementation (PRE 4.2.0):** **kernel/core/net/** — see **docs/P3_NETWORKING.md** and
**kernel/core/net/README.md**. Hosted wire I/O uses **arch/*/net_asm.*** (checksum) and
**arch/*/net_wire_host_asm.*** (Linux socket syscalls on x86_64 and AArch64).

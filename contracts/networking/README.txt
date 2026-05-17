P3 (*contracts/networking*) — roadmap **P3-1** … **P3-12** (netdev, loopback,
TAP, ARP, IPv4, UDP, DHCP, TCP, DNS, TLS hosted, Wi‑Fi deferral, IPv6 deferral).

**Umbrella header:** *contract_networking.h* — includes **contract_identity.h**
(P0 + P1 + P2), **FL_CONTRACT_P3_NETWORKING_REV**, all shards below, and
**FL_CONTRACT_P3_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P3_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
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

Each shard starts with **#include "contract_identity.h"** so **P2** authz and
**P1** timekeeping obligations stay in scope for network surfaces.

**Build:** add **-Icontracts/networking** next to **-Icontracts/identity** in the
root **Makefile** **CFLAGS** and in **CMakeLists.txt** include directories for
targets that compile networking or shell code that includes this bundle.

**Related:** **kernel/include/fl/driver/net.h** remains the thin driver ops
placeholder; normative **data-distribution** for the stack lives here.

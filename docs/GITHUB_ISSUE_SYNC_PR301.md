# GitHub issue sync — PR #301 (maintainer)

CodeRabbit comments on **#280** and **#283** predated commits **`e7c0399`**, **`f982432`**, and **`155e245`**. The integration token cannot run **`gh issue edit`**; use this file to align issue bodies with the branch.

**PR:** [#301](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/301)  
**Branch:** `cursor/p3-unified-channel-meta-2111`

## #283 — scope checklist (all items done in tree)

Check **all** scope items, then **close** with a note that NDP on production wire remains under **#280** epic tail.

| Item | Evidence |
|------|----------|
| `OP_CTRL_HOST_PROMOTE6`, REV / `fl_net_addr_t` | `contracts/networking/contract_p3_server.h`, `contract_p3_session_wire.h` |
| `peer_addr` on members | `fl_net_server_member_t.peer_addr` |
| Client bind via `local_ep` (`local_ip_be` removed) | commit `03082a8`, `fl_net_client_t` |
| Socket `*_addr` helpers | `net_socket.c` |
| `fl_net_server_transfer_and_stop` PROMOTE6 branch | `net_server.c` |
| Client PROMOTE6 dispatch | `net_client.c` — includes **`opcode_to_event`** for `0x24` |
| **`host_addr` on HOST_PROMOTE / HOST_REDIRECT callback** | `fl_net_client_event_cb` → `const fl_net_addr_t *` |
| Bracket `parse_endpoint` | `fl_net_endpoint_parse` / `cmd_server.c` |
| **`contract_p3_host_promote6.h`** | `contracts/networking/contract_p3_host_promote6.h` |
| Tests | `tests/test_p3_server.c` — `test_host_promote_callback_ep`, v6 loopback host/join |

## #280 — implementation checklist (foundation done)

Check these; leave issue **open** only if tracking epic tail (TAP egress, TCPv6, SLAAC/DHCPv6), or **close** as “foundation complete.”

| Item | Evidence |
|------|----------|
| `contract_p3_ipv6.h` wired | `contract_networking.h`, REV **16** |
| `net_ipv6` / `net_icmpv6` / `net_ndp` | `kernel/core/net/net_*.c` |
| IPv6 FIB | `fl_net_route_add6`, `fl_net_route_lookup6` |
| DNS AAAA | `fl_net_dns_resolve_aaaa` |
| `FL_ETHERTYPE_IPV6` loopback dispatch | `net_loopback.c`, `net_wire.c` |
| Tests | `make test_p3_network` — `test_ipv6_icmp_echo_loopback`, `test_ipv6_ndp_ns_na_loopback`, `test_ipv6_route_lookup_loopback`, `test_dns_resolve_aaaa_localhost` |
| Docs | `docs/P3_NETWORKING.md`, `docs/ROADMAP.md` P3-11 **~✅**, **`docs/P3_NETWORKING_DEFERRED.md`** (no longer “IPv4-only”) |

## #239 — deferred rows

| Item | Action |
|------|--------|
| `OP_CTRL_HOST_PROMOTE6` → **#283** | Already **[x]** — keep |
| IPv6 + ICMPv6 + NDP → **#280** | Check **[x]** with note: *foundation on PR #301; epic tail on #280* |
| Wi‑Fi → **#279** | Leave **[ ]** |
| Native `fl_socket` | Leave **[ ]** (P3-7 gate) |

## #279 — use PR #306 sync doc

Wi‑Fi **P3-10** foundation landed on **`cursor/p3-issues-279-302-303-790d`** (PR **#306**). For checklist alignment use **`docs/GITHUB_ISSUE_SYNC_279.md`** — do **not** mirror that checklist here.

## Already closed

- **#284** — endian / PROMOTE NBO  
- **#285** — session mutex around promote

## Verify locally

```bash
make test_invariants test_p3_server test_p3_network test_server_shared_catalog test_channel_sidecar
```

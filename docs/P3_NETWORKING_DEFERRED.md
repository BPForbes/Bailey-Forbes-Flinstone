# P3 deferred networking trackers

Explicit promotion trackers for roadmap rows marked **`[DEFERRED]`** at the **contract-definition** layer. These issues document scope; **module integration** status is in **[`docs/P3_NETWORKING.md`](P3_NETWORKING.md)** and **[`docs/ROADMAP.md`](ROADMAP.md)**.

| Issue | Roadmap | Contract header | Integration (PRE 4.2.0) |
|-------|---------|-----------------|-------------------------|
| [#257](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/257) / [#279](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/279) | **P3-10** Wi‑Fi station | `contract_p3_wifi_deferred.h` | **❌** — still deferred (no 802.11 station code) |
| [#258](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/258) / [#280](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/280) | **P3-11** IPv6 + ICMPv6 | **`contract_p3_ipv6.h`** (forward: `contract_p3_ipv6_deferred.h`) | **~✅ foundation** — see below |

## P3-10 Wi‑Fi (#257 / #279)

When promoted, **802.11** data frames still cross **`fl_net_frame_view_t`**, then bind to existing **P3-5** / **P3-6** / **P3-7** like today’s Ethernet path. Expect **P4** firmware/driver work before lab or production claims. **P3-12** DHCP remains the addressing step after L2 comes up.

**PRE 4.2.0:** only the **channel sidecar** `center_freq_hz` hook exists (`contract_p3_channel_sidecar.h`); full station mode is **not** implemented.

## P3-11 IPv6 (#258 / #280)

**Do not read this section as “IPv4-only.”** As of **PR #301** (`cursor/p3-unified-channel-meta-2111`), the in-tree stack includes an **IPv6 foundation** on software loopback:

| Area | Status | Where |
|------|--------|--------|
| Contract | ✅ promoted | **`contract_p3_ipv6.h`** in **`contract_networking.h`** |
| IPv6 header | ✅ | **`net_ipv6.c`** |
| ICMPv6 echo | ✅ loopback | **`net_icmpv6.c`** |
| NDP NS/NA | ✅ loopback | **`net_ndp.c`** |
| IPv6 FIB | ✅ | **`fl_net_route_add6`** / **`fl_net_route_lookup6`** in **`net_route.c`** |
| L3 dispatch | ✅ loopback | **`FL_ETHERTYPE_IPV6`** in **`net_loopback.c`**; eth build/parse in **`net_wire.c`** |
| DNS AAAA | ✅ stub | **`fl_net_dns_resolve_aaaa`** in **`net_dns.c`** (localhost + UDP path) |
| Tests | ✅ | **`test_ipv6_*`**, **`test_dns_resolve_aaaa_localhost`** in **`make test_p3_network`** |
| Host transfer v6 | ✅ foundation | **#283** — **`OP_CTRL_HOST_PROMOTE6`**, **`host_addr`** callback |

**Epic tail still open on [#280](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/280)** (issue may stay open until a maintainer closes it):

- TAP / **`net_wire_egress`** IPv6 production path
- Production **TCP over IPv6** (loopback FSM is IPv4-centric today)
- SLAAC / DHCPv6
- **`ping6`** shell verb (unit tests cover ICMPv6 echo on loopback)

For the **current layer map**, tests, and standards table, use **[`docs/P3_NETWORKING.md`](P3_NETWORKING.md)** — not this deferral page.

## Related

- [`docs/P3_NETWORKING.md`](P3_NETWORKING.md) — active P3 map (**authoritative for implementation**)
- [`docs/ROADMAP.md`](ROADMAP.md) — phase table (P3-11 module integration **~✅**)
- [`docs/GITHUB_ISSUE_SYNC_PR301.md`](GITHUB_ISSUE_SYNC_PR301.md) — maintainer checklist to align **#280** / **#283** GitHub bodies with this tree (CodeRabbit sync)
- [`docs/GITHUB_ISSUES_238_267_TRACKER.md`](GITHUB_ISSUES_238_267_TRACKER.md) — umbrella **#238–#267** (excl. **#239**)

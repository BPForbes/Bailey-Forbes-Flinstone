# P3 deferred networking trackers

Explicit promotion trackers for roadmap rows marked **`[DEFERRED]`**. These issues document scope and contracts; they do **not** claim production Wi‑Fi or IPv6 stacks on **`develop`**.

| Issue | Roadmap | Contract header |
|-------|---------|-----------------|
| [#257](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/257) | **P3-10** Wi‑Fi station | `contracts/networking/contract_p3_wifi_deferred.h` |
| [#258](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/258) | **P3-11** IPv6 + ICMPv6 | `contracts/networking/contract_p3_ipv6_deferred.h` |

## P3-10 Wi‑Fi (#257)

When promoted, **802.11** data frames still cross **`fl_net_frame_view_t`**, then bind to existing **P3-5** / **P3-6** / **P3-7** like today’s Ethernet path. Expect **P4** firmware/driver work before lab or production claims. **P3-12** DHCP remains the addressing step after L2 comes up.

## P3-11 IPv6 (#258)

Dual-stack is an intentional later step: **RFC 8200** IPv6, **RFC 4443** ICMPv6, **RFC 4861** neighbor discovery, and **AAAA** in **P3-8** when the row is promoted. Until then, the in-tree stack is **IPv4-only** on wire and loopback.

## Related

- [`docs/P3_NETWORKING.md`](P3_NETWORKING.md) — active P3 map
- [`docs/ROADMAP.md`](ROADMAP.md) — phase table
- [`docs/GITHUB_ISSUES_238_267_TRACKER.md`](GITHUB_ISSUES_238_267_TRACKER.md) — umbrella **#238–#267** (excl. **#239**)

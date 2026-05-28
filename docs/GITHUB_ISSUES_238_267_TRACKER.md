# GitHub issues #238–#267 (umbrella tracker)

Coordination doc for branch **`cursor/github-issues-238-267-b55d`**. Scope: issues **#238** through **#267** inclusive, **excluding [#239](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/239)** (P3-13 chat `server` — separate train; see [`docs/P3_13_CHAT_SERVER.md`](P3_13_CHAT_SERVER.md)).

Primary networking map: [`docs/P3_NETWORKING.md`](P3_NETWORKING.md).

## Open (in scope)

| Issue | Title | Kind |
|-------|--------|------|
| [#240](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/240) | P3 PRE 4.2.0 gap tracker: integration ~✅ → ✅ | Checklist / docs |
| [#252](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/252) | P3-9: TLS library bridge | Implementation |
| [#257](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/257) | P3-10: Wi-Fi station path (deferred) | Tracker |
| [#258](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/258) | P3-11: IPv6 + ICMPv6 (deferred) | Tracker |
| [#259](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/259) | PX-11: HTTP/HTTPS L7 stack | Tracker |
| [#260](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/260) | PX-12: netboot / PXE / UEFI | Tracker |
| [#262](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/262) | P3-5/P3-4: drop Linux ICMP fallback, consolidate loopback egress | Bug / refactor |
| [#267](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/267) | P3-5 IPv4: default-route, PMTU, offload policy | Tracker / implementation |

## Closed or merged in range (reference)

| Issue | State | Notes |
|-------|--------|--------|
| #241 | CLOSED | Bare-metal 802.3 / ARP/IPv4 wire |
| #242 | CLOSED | P1-8 workqueue |
| #243 | MERGED | Packet layering contract |
| #244 | MERGED | UDP demux / socket shim prep |
| #245–#246 | CLOSED | TCP FSM follow-ups |
| #248–#249 | CLOSED | PX-11 HTTP(S), PX-12 TFTP (implementation landed) |
| #250 | CLOSED | P3-14 RX dequeue / TCP timer wheel |
| #253–#256 | CLOSED | TLS/DHCP/DNS follow-ups + docs sync |
| #261 | CLOSED | `P3_NETWORKING.md` sync |
| #263–#266 | CLOSED | Protocol gap index / UDP façade trackers |
| #238 | IN PROGRESS (loopback) | TCP FSM + UDP demux on branch |
| #251 | DONE (branch) | Multi-NS DNS + egress UDP |
| #247 | DONE (branch) | `fl_net_dhcp_acquire` + route |

## Progress

- **#262** — Done: egress-only ICMP/UDP; no Linux datagram shim on unrouted `dst`.
- **#238** — Loopback TCP FSM: unique ephemeral ports, RST/ACK connect cleanup, routed `fl_net_tcp_connect` via egress; dual-client tests.
- **#251** — DNS: up to three nameservers from `/etc/resolv.conf`, per-NS retries, rotating query IDs.
- **#247** — `fl_net_dhcp_acquire` (DISCOVER/OFFER/REQUEST/ACK over egress; optional static route install).

## Suggested work order

1. ~~**#262**~~, ~~**#238** (loopback lab)~~, ~~**#251**~~, ~~**#247** (acquire + route)~~.
2. **#252** — TLS library bridge.
3. **#238** follow-up — TAP production TCP timers/retransmit/TIME_WAIT (beyond loopback lab).
4. **#267** — IPv4 default-route / PMTU / offload policy.
4. **#267** — IPv4 routing/PMTU/offload policy.
5. **#240** — PRE 4.2.0 standards checklist as rows flip to ✅.
6. **#257–#260** — Deferred promotion trackers and doc cross-links as code lands.

## Tests

```bash
make test_p3_network
make check-network-requirements
```

## PR checklist

Update this file and [`docs/P3_NETWORKING.md`](P3_NETWORKING.md) when an issue closes. Reference **`Closes #NNN`** in commit messages where appropriate.

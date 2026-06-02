# GitHub issues #238–#267 (umbrella tracker)

Coordination doc for branch **`cursor/github-issues-238-267-b55d`** → PR **#275** into **`develop`**.

Scope: issues **#238** through **#267** inclusive, **excluding [#239](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/239)** (P3-13 chat **`server`** — separate train; see [`docs/P3_13_CHAT_SERVER.md`](P3_13_CHAT_SERVER.md)).

Primary networking map: [`docs/P3_NETWORKING.md`](P3_NETWORKING.md). **P3-10** deferral and **P3-11** promotion status: [`docs/P3_NETWORKING_DEFERRED.md`](P3_NETWORKING_DEFERRED.md). **PR #301** issue-body sync: [`docs/GITHUB_ISSUE_SYNC_PR301.md`](GITHUB_ISSUE_SYNC_PR301.md).

## Resolved on this branch (closes in PR #275)

| Issue | Summary |
|-------|---------|
| [#238](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/238) | Loopback **RFC 793** TCP FSM subset, UDP parse/xmit/echo, egress-routed connect |
| [#240](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/240) | PRE 4.2.0 integration checklist — **ROADMAP** / **P3_NETWORKING** ~✅ → ✅ |
| [#247](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/247) | **`fl_net_dhcp_acquire`** over egress + static route install |
| [#251](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/251) | Multi-nameserver DNS, retries, rotating TXIDs |
| [#252](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/252) | **P3-9** OpenSSL client bridge when **libssl** is present |
| [#257](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/257) | **P3-10** Wi‑Fi deferred tracker + contract cross-link |
| [#258](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/258) | **P3-11** IPv6 tracker (legacy); active epic **[#280](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/280)** — **`contract_p3_ipv6.h`** + loopback foundation on **PR #301** |
| [#259](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/259) | **PX-11** minimal HTTP/1.0 GET (**`net_http.c`**) |
| [#260](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/260) | **PX-12** TFTP RRQ/client subset (**`net_tftp.c`**) |
| [#262](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/262) | Egress-only ICMP/UDP; no Linux datagram fallback when unrouted |
| [#267](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/267) | Reject **0.0.0.0/0** in **`fl_net_route_add`**; PMTU/offload policy documented |

## Out of scope (not closed by this PR)

| Issue | Reason |
|-------|--------|
| [#239](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/239) | P3-13 **`server`** / chat hub — separate implementation train |

## Earlier range (reference)

Issues **#241**–**#266** (except those listed above) were closed or merged on prior **PRE 4.2.0** work; see git history and [`version/entries/preproduction 4.2.0/`](version/entries/preproduction%204.2.0/).

## Tests

```bash
make test_p3_network
make check-network-requirements
```

## PR checklist

- [x] Tracker and **P3** docs updated
- [x] **`version/entries/preproduction 4.2.0/4_2_0_github_issues_238_267_umbrella.ver`** — **DESCRIPTION** only
- [ ] Do **not** commit **`userland/shell/version_def.h`** or **`version/locked/**`**

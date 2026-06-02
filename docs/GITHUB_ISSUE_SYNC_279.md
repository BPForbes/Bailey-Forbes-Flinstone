# GitHub issue sync — #279 P3-10 Wi‑Fi (maintainer)

Align **[#279](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/279)** / **#257** with branch **`cursor/p3-issues-279-302-303-790d`** (PR [#306](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/306)).

**Legend:** **Lab** = hosted `FL_NET_WIFI_HOSTED_LAB` (loopback `fl_net_driver_t`, no RF). **Blocked** = needs P4 NIC + QEMU/real 802.11ax before production sign-off.

## Promotion prerequisites (4)

| # | Prerequisite | GitHub |
|---|--------------|--------|
| 1 | P4 firmware / driver | Leave **❌** |
| 2 | QEMU 802.11ax or real WiFi 6 NIC | Leave **❌** |
| 3 | P3-12 DHCP | **✅** (#247) |
| 4 | P3-5 routing / egress | **✅** (#262) |

## Scope checklist (19)

| # | Item | Check? | Notes |
|---|------|--------|-------|
| 1 | Promote `contract_p3_wifi.h`; wire umbrella REV 18 | **[x]** | `contracts/networking/contract_p3_wifi.h` |
| 2 | `fl_net_wifi_he_cap_t`, TWT params, scan HE/band fields | **[x]** | Contract types |
| 3 | `net_wifi_station` FSM IDLE→UP, band scan | **[x] Lab** | Full lab FSM + `FL_WIFI_STATE_UP`; not on RF/NIC |
| 4 | `net_wifi_mgmt` Probe/Auth/Assoc + HE IE | **[x] Lab** | Probe/assoc build + IE parse; not full OTA auth |
| 5 | `net_wifi_he` HE Cap/Op decode | **[x]** | `net_wifi_he.c`, unit tests |
| 6 | `net_wifi_sae` WPA3-SAE | **[x] Lab** | KDF + PMK derive; Dragonfly OTA **blocked** |
| 7 | `net_wifi_wpa` WPA2 4-way | **[x] Lab** | PMK/PTK crypto + install; wire handshake **blocked** |
| 8 | `net_wifi_twt` setup/teardown | **[x] Lab** | Mock negotiation + flow_id |
| 9 | `fl_net_wifi_scan` / `_connect` / `_disconnect` | **[x] Lab** | Public API |
| 10 | `fl_net_wifi_he_cap()` | **[x] Lab** | Post-assoc |
| 11 | Post-assoc `fl_net_dhcp_acquire` | **[x] Lab** | Static route on loopback netdev (not live DHCP server) |
| 12 | Register `fl_net_driver_t` | **[x] Lab** | `fl_net_wifi_station_netdev()` → loopback |
| 13 | E2E scan→SAE→DHCP→UDP | **[x] Lab** | `make test_p3_wifi` FSM + netdev; no RF UDP echo |
| 14 | WPA3-SAE unit test (RFC 7664 KDF) | **[x]** | `fl_net_wifi_sae_rfc7664_kdf_selftest` |
| 15 | WPA2 4-way unit test | **[x]** | PMK vector + PTK derive in `test_p3_wifi` |
| 16 | TWT mock test | **[x]** | `test_twt_mock` |
| 17 | HE IE parse unit test | **[x]** | Captured IE bytes |
| 18 | `docs/ROADMAP` / `P3_NETWORKING` P3-10 ~✅ | **[x]** | Prior doc pass |
| 19 | Auth guard `contract_p3_trust.h` | **[x]** | `FL_NET_WIFI_AUTHZ_OP_SCAN_CONNECT`; shell `FL_AUTHZ_OP_NETDEV_IO`; `fl_net_wifi_cred_scrub_passphrase` |

## Acceptance criteria (10)

| # | Criterion | Check? | Notes |
|---|-----------|--------|-------|
| 20 | WPA3-SAE connect on QEMU/real NIC | **[ ]** | Lab only until prerequisite 2 |
| 21 | WPA2-PSK on non-ax AP | **[ ]** | Lab open/WPA paths; no RF |
| 22 | `scan_result` HE fields on real ax AP | **[x] Lab** | IE-enriched lab beacon |
| 23 | `he_cap()` NSS/OFDMA/TWT | **[x] Lab** | `test_station_fsm_netdev` |
| 24 | TWT negotiated `flow_id` | **[x] Lab** | `test_twt_mock` |
| 25 | DHCP + UDP on Wi‑Fi netdev | **[ ]** / **Lab** | Loopback route UP; live DHCP/UDP **blocked** |
| 26 | SAE RFC 7664 / 802.11 vectors | **[x]** | KDF selftest + PMK derive tests |
| 27 | WPA2 reference vectors | **[x]** | IEEE/passphrase PMK test |
| 28 | HE IE decoder reference bytes | **[x]** | `test_he_capabilities_parse` |
| 29 | `make test_p3_network` no regression | **[x]** | Run in CI / before merge |
| 30 | ROADMAP P3-10 ~✅ | **[x]** | |

**Total tracked items: 30** (4 prerequisites + 19 scope + 10 acceptance; item 25 split lab vs production).

## Verify

```bash
make test_p3_wifi test_wifi_db test_p3_network test_invariants
./scripts/check_version_entries_semver_dev_unique.sh
```

## Do not check on #239

Wi‑Fi **`server host`** production row stays **[ ]** until items 20–21 and NIC land.

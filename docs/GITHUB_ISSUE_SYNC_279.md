# GitHub issue sync — #279 P3-10 Wi‑Fi (maintainer)

Align **[#279](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/279)** / **#257** with branch **`v4.3.0-wifi-drivers`** (PR [#320](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/320)).

**Legend:** **Lab** = hosted `FL_NET_WIFI_HOSTED_LAB` (loopback `fl_net_driver_t`, no RF). **Mock ax** = `FL_WIFI_80211AX_MOCK=1` software FullMAC (`wifi_80211ax_mock.c`) — exercises 802.11ax + OTA auth on Wi‑Fi 5-only hardware without RF. **RF** = real 802.11ax NIC / QEMU passthrough still required to **close** #279.

**33 tracked items** = 4 prerequisites + 19 scope + 10 acceptance. Automated matrix: **`make test_wifi_80211ax_mock_279`**.

## Promotion prerequisites (4)

| # | Prerequisite | GitHub | Mock ax | RF production |
|---|--------------|--------|---------|---------------|
| 1 | P4 firmware / driver | **~✅ Mock** | `wifi_80211ax_mock` + backend | Real Phase 4 FullMAC still open |
| 2 | QEMU 802.11ax or real WiFi 6 NIC | **~✅ Mock** | `FL_WIFI_80211AX_MOCK=1` | Still **❌** until NIC/QEMU |
| 3 | P3-12 DHCP | **✅** (#247) | mock + lab netdev | **✅** |
| 4 | P3-5 routing / egress | **✅** (#262) | mock UDP echo path | **✅** |

## Scope checklist (19)

| # | Item | Check? | Notes |
|---|------|--------|-------|
| 1 | Promote `contract_p3_wifi.h`; wire umbrella REV 18 | **[x]** | `contracts/networking/contract_p3_wifi.h` |
| 2 | `fl_net_wifi_he_cap_t`, TWT params, scan HE/band fields | **[x]** | Contract types |
| 3 | `net_wifi_station` FSM IDLE→UP, band scan | **[x] Mock** | `test_wifi_80211ax_mock_279` scope-3 |
| 4 | `net_wifi_mgmt` Probe/Auth/Assoc + HE IE | **[x] Mock** | scope-4; RF OTA still open |
| 5 | `net_wifi_he` HE Cap/Op decode | **[x]** | scope-5 / accept-28 |
| 6 | `net_wifi_sae` WPA3-SAE | **[x] Mock** | scope-6; Dragonfly RF **open** |
| 7 | `net_wifi_wpa` WPA2 4-way | **[x] Mock** | scope-7; RF wire **open** |
| 8 | `net_wifi_twt` setup/teardown | **[x] Mock** | scope-8 / accept-24 |
| 9 | `fl_net_wifi_scan` / `_connect` / `_disconnect` | **[x] Mock** | scope-9 via mock backend |
| 10 | `fl_net_wifi_he_cap()` | **[x] Mock** | scope-10 / accept-23 |
| 11 | Post-assoc `fl_net_dhcp_acquire` | **[x] Mock** | scope-11 + lab netdev DHCP |
| 12 | Register `fl_net_driver_t` | **[x] Mock** | scope-12 mock netdev ops |
| 13 | E2E scan→SAE→DHCP→UDP | **[x] Mock** | scope-13; RF **open** |
| 14 | WPA3-SAE unit test (RFC 7664 KDF) | **[x]** | scope-14 / accept-26 |
| 15 | WPA2 4-way unit test | **[x]** | scope-15 / accept-27 |
| 16 | TWT mock test | **[x] Mock** | scope-16 |
| 17 | HE IE parse unit test | **[x]** | scope-17 |
| 18 | `docs/ROADMAP` / `P3_NETWORKING` P3-10 ~✅ | **[x]** | scope-18 / accept-30 |
| 19 | Auth guard `contract_p3_trust.h` | **[x]** | scope-19 |

## Acceptance criteria (10)

| # | Criterion | Check? | Notes |
|---|-----------|--------|-------|
| 20 | WPA3-SAE connect on QEMU/real NIC | **[x] Mock** | accept-20 supplicant + mock connect; **RF open** |
| 21 | WPA2-PSK on non-ax AP | **[x] Mock** | accept-21 LegacyAC mock AP; **RF open** |
| 22 | `scan_result` HE fields on real ax AP | **[x] Mock** | accept-22 MockAx6 6 GHz HE enrich |
| 23 | `he_cap()` NSS/OFDMA/TWT | **[x] Mock** | accept-23 |
| 24 | TWT negotiated `flow_id` | **[x] Mock** | accept-24 |
| 25 | DHCP + UDP on Wi‑Fi netdev | **[x] Mock** | accept-25; RF **open** |
| 26 | SAE RFC 7664 / 802.11 vectors | **[x]** | accept-26 |
| 27 | WPA2 reference vectors | **[x]** | accept-27 |
| 28 | HE IE decoder reference bytes | **[x]** | accept-28 |
| 29 | `make test_p3_network` no regression | **[x]** | accept-29 + CI |
| 30 | ROADMAP P3-10 ~✅ | **[x]** | accept-30 |

**Total tracked items: 33** (4 + 19 + 10). Mock ax satisfies all 33 in software; **#279 remains open** until production RF OTA on a real 802.11ax path (Phase 4 FullMAC or confirmed QEMU NIC).

## Verify

```bash
make test_wifi_80211ax_mock_279   # all 33 #279 items (mock ax)
make test_p3_wifi test_wifi_coprocessor test_p3_network test_invariants
./scripts/check_version_entries_semver_dev_unique.sh
```

Set **`FL_WIFI_80211AX_MOCK=1`** (and omit **`FL_WIFI_UART_FD`**) to route `wifi_driver_backend` through the software ax NIC instead of UART coprocessor.

## Do not check on #279

Wi‑Fi **`server host`** production row stays **[ ]** until RF items 20–21 land on real hardware.

# GitHub issue sync — P4 Wi‑Fi OTA / 802.11ax production tail (maintainer)

Align closed **[#279](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/279)** / **#257** foundation with closed **[#328](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/328)** (P4-01 in-tree driver independence) and review follow-ups **[#329](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/329)** on the **4.3.0** train (PR [#320](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/320), [#333](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/333), [#338](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/338)).

**Legend:** **Lab** = hosted `FL_NET_WIFI_HOSTED_LAB` (loopback `fl_net_driver_t`, no RF). **Mock ax** = `FL_WIFI_80211AX_MOCK=1` software FullMAC (`wifi_lab_backend.c` / `wifi_lab_mock_*`) — exercises 802.11ax + OTA auth on Wi‑Fi 5-only hardware without RF. **Server OTA** = P3 **`server host`** + **`server join`** with `net_wifi_ax_server.c` relaying SAE commit/confirm, EAPOL 4-way, and Assoc Req/Resp (HE IEs) on session opcodes `0x40`–`0x45` — L2 over TCP, not RF. **RF** = maintainer-confirmed Linux 802.11ax FullMAC adapter plus physical ESP UART (closes **#328**).

**33 tracked items** = 4 prerequisites + 19 scope + 10 acceptance (inherited from #279 mock matrix). Automated matrix: **`make test_wifi_80211ax_mock_279`**. L2 session-wire OTA: **`make test_wifi_ax_server_ota`**.

## File layout (#328)

| Layer | Path | Notes |
|-------|------|-------|
| FSM / orchestration | `kernel/core/net/net_wifi_station.c` | Driver → host → lab dispatch |
| Hosted fallback | `kernel/core/net/net_wifi_host_linux.c` | nmcli / wpa_cli / FlinstonePowershell |
| Driver router | `kernel/drivers/wifi/wifi_driver_backend.c` | Phase 1/2/3/4 backend selection |
| Coprocessor | `kernel/drivers/wifi/wifi_coprocessor.c` | Phase 1 UART AT |
| FullMAC | `kernel/drivers/wifi/fullmac/wifi_fullmac_*.c` | Phase 4 PCIe/USB probe |
| Lab + mock | `kernel/drivers/wifi/wifi_lab_backend.c` | `wifi_lab_mock_*` software ax NIC |
| Contract | `contracts/networking/contract_p3_wifi.h` | Promoted; deferred stub removed |

## Promotion prerequisites (4)

| # | Prerequisite | GitHub | Mock ax | RF production |
|---|--------------|--------|---------|---------------|
| 1 | P4 firmware / driver | **~✅ Mock** | `wifi_lab_mock_*` + backend | Real Phase 4 FullMAC still open |
| 2 | QEMU 802.11ax or real WiFi 6 NIC | **~✅ Mock** | `FL_WIFI_80211AX_MOCK=1` | Still **❌** until NIC/QEMU |
| 3 | P3-12 DHCP | **✅** (#247) | mock + lab netdev | **✅** |
| 4 | P3-5 routing / egress | **✅** (#262) | mock UDP echo path | **✅** |

## Scope checklist (19)

| # | Item | Check? | Notes |
|---|------|--------|-------|
| 1 | Promote `contract_p3_wifi.h`; wire umbrella REV 18 | **[x]** | `contracts/networking/contract_p3_wifi.h` |
| 2 | `fl_net_wifi_he_cap_t`, TWT params, scan HE/band fields | **[x]** | Contract types |
| 3 | `net_wifi_station` FSM IDLE→UP, band scan | **[x] Mock** | `test_wifi_80211ax_mock_279` scope-3 |
| 4 | `net_wifi_mgmt` Probe/Auth/Assoc + HE IE | **[x] Mock + Server OTA** | scope-4; Assoc Req/Resp HE via `test_wifi_ax_server_ota`; RF **open** |
| 5 | `net_wifi_he` HE Cap/Op decode | **[x]** | scope-5 / accept-28 |
| 6 | `net_wifi_sae` WPA3-SAE | **[x] Mock + Server OTA** | scope-6; Dragonfly over session wire in `test_wifi_ax_server_ota`; RF **open** |
| 7 | `net_wifi_wpa` WPA2 4-way | **[x] Mock + Server OTA** | scope-7; EAPOL over session wire; RF **open** |
| 8 | `net_wifi_twt` setup/teardown | **[x] Mock** | scope-8 / accept-24 |
| 9 | `fl_net_wifi_scan` / `_connect` / `_disconnect` | **[x] Mock** | scope-9 via mock backend |
| 10 | `fl_net_wifi_he_cap()` | **[x] Mock** | scope-10 / accept-23 |
| 11 | Post-assoc `fl_net_dhcp_acquire` | **[x] Mock** | scope-11 + lab netdev DHCP via `wifi_driver_dhcp_exchange` |
| 12 | Register `fl_net_driver_t` | **[x] Mock** | scope-12 mock netdev ops |
| 13 | E2E scan→SAE→DHCP→UDP | **[x] Mock** | scope-13; RF **open** |
| 14 | WPA3-SAE unit test (RFC 7664 KDF) | **[x]** | scope-14 / accept-26 |
| 15 | WPA2 4-way unit test | **[x]** | scope-15 / accept-27 |
| 16 | TWT mock test | **[x] Mock** | scope-16 |
| 17 | HE IE parse unit test | **[x]** | scope-17 |
| 18 | `docs/ROADMAP` / `P3_NETWORKING` P3-10 ✅ | **[x]** | scope-18 / accept-30 |
| 19 | Auth guard `contract_p3_trust.h` | **[x]** | scope-19 |

## Acceptance criteria (10)

| # | Criterion | Check? | Notes |
|---|-----------|--------|-------|
| 20 | WPA3-SAE connect on QEMU/real NIC | **[x] Mock + Server OTA** | accept-20 + `test_wifi_ax_server_ota`; **RF open** |
| 21 | WPA2-PSK on non-ax AP | **[x] Mock + Server OTA** | accept-21 + server EAPOL path; **RF open** |
| 22 | `scan_result` HE fields on real ax AP | **[x] Mock** | accept-22 MockAx6 6 GHz HE enrich |
| 23 | `he_cap()` NSS/OFDMA/TWT | **[x] Mock** | accept-23 |
| 24 | TWT negotiated `flow_id` | **[x] Mock** | accept-24 |
| 25 | DHCP + UDP on Wi‑Fi netdev | **[x] Mock** | accept-25; RF **open** |
| 26 | SAE RFC 7664 / 802.11 vectors | **[x]** | accept-26 |
| 27 | WPA2 reference vectors | **[x]** | accept-27 |
| 28 | HE IE decoder reference bytes | **[x]** | accept-28 |
| 29 | `make test_p3_network` no regression | **[x]** | accept-29 + CI |
| 30 | ROADMAP P3-10 ✅ | **[x]** | accept-30 |

**Total tracked items: 33** (4 + 19 + 10). Mock ax satisfies all 33 in software.

## #328 eight tasks (closed)

| # | Task | Evidence |
|---|------|----------|
| 1 | `mac80211_hwsim` CI | Software/lab OTA suite on the #328 train; issue-specific GH Actions job **sunset** after close |
| 2 | ESP UART `wifi scan` / `wifi join` | **`make test_wifi_uart_at_scan_join`** (PTY AT simulator) plus maintainer-confirmed physical `/dev/ttyUSB*` |
| 3 | WPA2-PSK without OS supplicant | **`test_p3_wifi`** `LabWpa2`; asserts `!fl_net_wifi_station_host_backend()` |
| 4 | TWT Individual Setup/Teardown `flow_id` | Lab record in **`test_p3_wifi`**; maintainer-confirmed real-AP Action frames + stored `flow_id` |
| 5 | In-tree DHCP on Wi-Fi `fl_net_driver_t` | Post-assoc **`fl_net_dhcp_acquire`** (lab + physical FullMAC; no OS DHCP client) |
| 6 | UDP echo on that netdev | **`fl_net_udp_echo_exchange`** after WPA2 connect (lab + physical FullMAC) |
| 7 | WPA2 EAPOL 1–4 + key install | **`test_wifi_connect_ota`** + physical association key install |
| 8 | ROADMAP P3-10 / P4-01 | Rows flipped **~✅ → ✅**; **#328** closed |

**#328 is closed.** The issue-specific validator (`scripts/validate_issue_328.sh`) and GH Actions job `wifi-issue-328` are removed. Keep product tests: **`make test_p3_wifi`**, **`make test_wifi_uart_at_scan_join`**, **`make test_wifi_connect_ota`**, **`make test_p3_wifi_ota`**.

## Verify

```bash
make test_wifi_80211ax_mock_279   # all 33 #279 items (mock ax)
make test_wifi_ax_server_ota      # SAE + EAPOL + HE Assoc via server host/join
make test_p3_wifi test_wifi_coprocessor test_wifi_uart_at_scan_join test_p3_network
./scripts/check_version_entries_semver_dev_unique.sh
```

Set **`FL_WIFI_80211AX_MOCK=1`** (and omit **`FL_WIFI_UART_FD`**) to route `wifi_driver_backend` through the software ax NIC instead of UART coprocessor.

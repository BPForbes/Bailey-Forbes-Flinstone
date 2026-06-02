# GitHub issue sync — #279 P3-10 Wi‑Fi (maintainer)

The integration token cannot run **`gh issue edit`**. Use this file to align **[#279](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/279)** (and legacy **#257**) with the tree on **PR [#306](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/306)** / branch **`cursor/p3-issues-279-302-303-790d`**.

**Do not** mark production acceptance criteria complete until **P4 firmware/driver** scope and a **confirmed 802.11ax NIC** (QEMU or hardware) exist. Foundation work is **~✅**; full station path remains **blocked**.

## Promotion prerequisites (issue overview table)

| Prerequisite | GitHub action |
|--------------|----------------|
| P4 firmware / driver work | Leave **❌** — not started in-tree |
| QEMU 802.11ax or real WiFi 6 NIC | Leave **❌** — not confirmed |
| P3-12 DHCP (`fl_net_dhcp_acquire`) | **✅** — already done (#247) |
| P3-5 routing / egress | **✅** — already done (#262) |

## Scope checklist — check **[x]** only where evidence exists

| Item | Check? | Evidence |
|------|--------|----------|
| Promote **`contract_p3_wifi_deferred.h`** → **`contract_p3_wifi.h`**; wire **`contract_networking.h`** | **[x]** | `contracts/networking/contract_p3_wifi.h`, `contract_networking.h` — **`FL_CONTRACT_P3_NETWORKING_REV` 18** |
| **`fl_net_wifi_he_cap_t`**, **`fl_net_wifi_twt_params_t`**, band/HE fields on **`fl_net_wifi_scan_entry_t`** | **[x]** | `contract_p3_wifi.h` |
| **`net_wifi_he.c/.h`** — HE Capabilities / HE Operation IE decode | **[x]** | `kernel/core/net/net_wifi_he.c`; **`make test_p3_wifi`** |
| HE IE parse unit test (captured beacon/probe-resp bytes) | **[x]** | `tests/test_p3_wifi.c` |
| **`net_wifi_station.c/.h`** — full FSM **IDLE→…→UP** + band-aware scan on NIC | **[ ]** | Lab only: **`FL_NET_WIFI_HOSTED_LAB`** seeds scan + **`CONNECTED`**; no **AUTHING→ASSOC→DHCP→UP** on hardware |
| **`net_wifi_mgmt.c/.h`** — Probe/Auth/Assoc + HE IE build/parse | **[ ]** | `fl_net_wifi_mgmt_hdr_valid()` only |
| **`net_wifi_sae.c/.h`** — WPA3-SAE | **[ ]** | Returns **`FL_RESULT_NOSYS`** |
| **`net_wifi_wpa.c/.h`** — WPA2 4-way | **[ ]** | Returns **`FL_RESULT_NOSYS`** |
| **`net_wifi_twt.c/.h`** — TWT setup/teardown | **[ ]** | Returns **`FL_RESULT_NOSYS`**; **`fl_net_wifi_twt_*`** → **NOSYS** |
| Public **`fl_net_wifi_scan` / `_connect` / `_disconnect`** | **[x]** (lab) | `net_wifi_station.c` — production build without lab → **NOSYS** on scan |
| **`fl_net_wifi_he_cap()`** post-association | **[x]** (lab) | When **`CONNECTED`** / **`UP`** / **`DHCP`** |
| Post-association **`fl_net_dhcp_acquire`** + **`fl_net_driver_t`** | **[ ]** | **`fl_net_wifi_station_netdev()`** returns **NULL** |
| Shell **`wifi scan` / `wifi join` / `wifi known`** + **`wifi_router`** SQLite | **[x]** | `userland/command/cmd_wifi.c`, `net_wifi_db.c`, **`make test_wifi_db`** |
| E2E: scan → WPA3-SAE → DHCP → UDP echo | **[ ]** | Blocked on NIC + SAE/WPA + netdev shim |
| SAE / WPA2 / TWT vector tests | **[ ]** | Not implemented |
| **`docs/ROADMAP.md`** / **`docs/P3_NETWORKING.md`** P3-10 **~✅** | **[x]** | This PR doc pass + **`docs/P3_NETWORKING_DEFERRED.md`** |
| Credential storage guard via **`contract_p3_trust.h`** | **partial** | Contract prose: no long-lived secrets in **`fl_net_wifi_cred_t`**; profiles hashed in **`wifi_router`** (`password_hash.cpp`); shell uses **`FL_AUTHZ_OP_NETDEV_IO`** |

## Acceptance criteria (issue bottom) — all remain **[ ]**

Until a NIC backend and SAE/WPA wire exist, leave every acceptance bullet unchecked, including:

- WPA3-SAE / WPA2-PSK connect against real or emulated AP
- **`fl_net_wifi_scan_result()`** HE fields on live air (lab seed is not RF validation)
- TWT negotiation
- DHCP + UDP through Wi‑Fi **`fl_net_driver_t`**

## Related issues — no change

| Issue | Action |
|-------|--------|
| **#239** Wi‑Fi row | Leave **[ ]** — station not production-ready for **`server host`** on Wi‑Fi |
| **#257** | Point body at this sync file; same checklist as **#279** |

## Verify locally

```bash
make test_p3_wifi test_wifi_db test_p3_network test_invariants
./scripts/check_version_entries_semver_dev_unique.sh
```

## Docs cross-links

- [`docs/P3_NETWORKING_DEFERRED.md`](P3_NETWORKING_DEFERRED.md) — P3-10 foundation vs blocking tail
- [`docs/P3_NETWORKING.md`](P3_NETWORKING.md) — P3-x table (**P3-10** row)
- [`docs/GITHUB_ISSUE_SYNC_PR301.md`](GITHUB_ISSUE_SYNC_PR301.md) — **#280** / **#283** (do not check **#279** items there)

# v4.3.0: WiFi Drivers & External Dependency Reduction

**Release Focus:** P4 (Drivers) - WiFi driver development with emphasis on reducing external library dependencies

**Branch:** `v4.3.0-wifi-drivers`  
**Base:** `develop`  
**Target Version:** 4.3.0

---

## Executive Summary

v4.3.0 focuses on building native WiFi driver support within Flinstone, transitioning from external WiFi libraries to self-contained driver implementations. This release prioritizes a phased approach starting with platform abstraction, moving through chipset-specific drivers, and ultimately building toward PCIe WiFi 6 (802.11ax) support.

**Key Goals:**
- Establish unified network device abstraction (already have `netdev.h`)
- Implement modular WiFi coprocessor support (ESP32/ESP8266 over UART/SPI) with 802.11ax-ready interface
- Reduce external library dependencies in WiFi path
- Build foundation for future FullMAC and SoftMAC drivers with HE (High Efficiency) capability
- Create clean driver/firmware loading interfaces
- **Align with P3-10 / issue #279:** WiFi 802.11ax station support; unblock production association & DHCP

**Standards Compliance:**
- **IEEE 802.11ax-2021** (WiFi 6 / HE)
- **IEEE 802.11i** (WPA/WPA2/WPA3)
- **RFC 7664** (WPA3-SAE Dragonfly)
- **TWT** (Target Wake Time) for power-efficient operation

---

## Alignment with Issue #279 (P3-10 WiFi)

This roadmap directly addresses the blocking prerequisites for [issue #279](https://github.com/BPForbes/Bailey-Forbes-Flinstone/issues/279) and [PR #306](https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/306):

| Prerequisite | Current Status | v4.3.0 Resolution |
|--------------|----------------|-------------------|
| **P4 firmware / driver** | ❌ Blocked | ✅ Phases 1-4 deliver driver stack |
| **QEMU 802.11ax or real WiFi 6 NIC** | ❌ Blocked | ⚠️ Phase 4: Real PCIe WiFi 6 support |
| **P3-12 DHCP** | ✅ Complete | ✅ Reuse existing `net_dhcp.c` |
| **P3-5 routing / egress** | ✅ Complete | ✅ Integrate with `net_wire_egress.c` |

**Production unblock:** Phase 3 (WPA2/WPA3) + Phase 4 (FullMAC) will unblock:
- Criterion 20: WPA3-SAE connect on QEMU/real NIC
- Criterion 21: WPA2-PSK on non-ax AP
- Criterion 25: DHCP + UDP on WiFi netdev (production)
- Production `fl_net_wifi_station_netdev()` → fully active netdev

**Deferred to future releases (after v4.3.0):**
- Criterion 2 (QEMU 802.11ax simulator) — coordinate with QEMU maintainers
- Criterion 20/21 (live RF testing) — requires physical WiFi 6 hardware + certified coexistence testing

---

## Architecture Overview

### Current Flinstone Network Stack
```
Existing Components (kernel/core/net/):
├─ TCP/IP: IPv4, IPv6, UDP, TCP, ICMP
├─ WiFi MAC: WPA/WPA2/WPA3 crypto, SAE, HE
├─ Abstraction: netdev, netifs, routes
├─ Tools: DHCP, DNS, TFTP, HTTP
└─ Host Integration: Linux host adapter (net_wifi_host_linux.c)
```

### Phase Architecture (v4.3.0)
```
Flinstone Kernel
 ├─ syscall / socket API
 ├─ TCP/IP stack (existing)
 │   └─ IPv4, ARP, ICMP, UDP, TCP, DHCP, DNS
 ├─ Network Interface Layer (enhance)
 │   └─ netdev0: send_packet(), receive_packet()
 ├─ WiFi MAC Layer (integrate existing)
 │   └─ scan, authenticate, associate, encrypt, reconnect
 ├─ Hardware Driver (NEW - this release)
 │   ├─ WiFi Coprocessor Bridge (Phase 1-2)
 │   ├─ UART/SPI Transport (Phase 1)
 │   └─ PCIe/USB Drivers (Phase 4-5)
 └─ Firmware Loading & Management (NEW)
```

---

## Phase-by-Phase Roadmap

### Phase 1: WiFi Coprocessor Bridge (ESP32/ESP8266 over UART/SPI)
**Rationale:** Lowest complexity, immediate WiFi capability, existing transport support  
**Est. Effort:** 3-4 weeks

**Deliverables:**
- `kernel/drivers/wifi_coprocessor.c` - Abstract coprocessor interface
- `kernel/drivers/wifi_uart_transport.c` - UART driver for ESP32 AT commands
- AT command wrapper layer
  - Scan/join/status operations
  - Connection state tracking
  - Error handling & retries
- Integration with existing `net_wifi_*.c` MAC layer
- Unit tests for coprocessor lifecycle

**Key Files to Create/Modify:**
```
kernel/drivers/
├─ wifi_coprocessor.h       (new - coprocessor interface)
├─ wifi_coprocessor.c       (new - coprocessor logic)
├─ wifi_uart_transport.h    (new - UART glue)
├─ wifi_uart_transport.c    (new - ESP32 over UART)
└─ Makefile                 (update to include new drivers)

kernel/core/net/
├─ net_netdev.c             (update - register coprocessor)
├─ net_wifi_mgmt.c          (update - hook coprocessor ops)
└─ net_wifi_station.c       (update - state machine integration)
```

**Milestones:**
1. Coprocessor abstraction + UART transport
2. AT command parsing & WiFi scan
3. Join/authentication flow
4. Full netdev integration & testing

---

### Phase 2: Open Network Support & DHCP Integration (Lab Proof-of-Concept)
**Rationale:** Validate full end-to-end path before adding security; mirrors P3-10 lab testing  
**Est. Effort:** 2-3 weeks

**Deliverables:**
- Support open (no encryption) network scanning
- Join flow without WPA
- DHCP handshake over WiFi netdev
- Ping & UDP communication (matches P3-10 criterion 25 lab)
- Network diagnostics tools
- **Bonus:** Scan result includes HE fields (from `net_wifi_he.c` IE parser)

**Integration Points:**
- `net_dhcp.c` - already exists, integrate with WiFi netdev
- `net_ping_host.c` - test over WiFi interface
- `net_socket.c` - validate socket operations over WiFi
- `net_wifi_he.c` - enrich scan results with 802.11ax HE capabilities

**Lab vs. Production note:** This phase delivers P3-10 criterion 25 (DHCP + UDP on WiFi netdev) in lab configuration (simulated). Production criteria 20-21 require Phase 3+.

**Milestones:**
1. WiFi scan → SSID discovery
2. Open network join
3. DHCP lease acquisition
4. End-to-end connectivity test

---

### Phase 3: WPA2/WPA3 & Encryption Support (Unblocks Issue #279 Production)
**Rationale:** Security required for real-world usage; **unblocks P3-10 criteria 20-21**  
**Est. Effort:** 3-4 weeks

**Existing Components (leverage from P3-10):**
- `net_wifi_crypto.c` - AES-CCMP/GCMP (already unit-tested in PR #306)
- `net_wifi_wpa.c` - WPA/WPA2 4-way handshake (lab; wire-blocked)
- `net_wifi_sae.c` - SAE (WPA3) Dragonfly KDF (RFC 7664; lab; wire-blocked)
- `contracts/networking/contract_p3_wifi.h` - frozen contract types

**Deliverables:**
- Connect `net_wifi_wpa.c` 4-way handshake to real hardware
  - ✅ RFC 7748 PMK derivation exists (from IEEE test vectors in PR #306)
  - ✅ PTK derivation & key install logic exists
  - 🔧 Wire the handshake state machine to driver RX/TX
- Connect `net_wifi_sae.c` Dragonfly to real hardware
  - ✅ RFC 7664 KDF exists + unit-tested
  - 🔧 Connect Dragonfly exchange to coprocessor/FullMAC
- Replay counter validation (IEEE 802.11i)
- Re-keying support (GTK/PTK renewal)

**New Files:**
```
kernel/drivers/
├─ wifi_supplicant.h        (new - local supplicant state)
├─ wifi_supplicant.c        (new - handshake orchestration)
└─ wifi_key_mgmt.c          (new - key material handling & install)
```

**Milestones (unblocking P3-10):**
1. WPA2-PSK (Pre-Shared Key) on real AP (criterion 21)
2. WPA3-SAE (Simultaneous Authentication of Equals) on real WiFi 6 AP (criterion 20)
3. Key rotation & replay protection (IEEE 802.11i compliance)
4. Production DHCP after association (criterion 25 production)

---

### Phase 4: FullMAC WiFi 6 (802.11ax) Chipset (e.g., Broadcom BCM6437xx, Qualcomm QCA6x9x)
**Rationale:** Native WiFi 6 hardware with HE support; unblocks criterion 2 (real NIC)  
**Est. Effort:** 6-8 weeks

**Example Targets:** 
- **Broadcom BCM6437xx** (PCIe) — WiFi 6, OFDMA, TWT support
- **Qualcomm QCA639x** (PCIe/SDIO) — WiFi 6, WPA3, multi-user features
- **Intel AX210** (PCIe) — WiFi 6E-capable

**Deliverables:**
- `kernel/drivers/wifi_fullmac_pcie.c` - PCIe transport for WiFi 6 chipsets
- `kernel/drivers/wifi_fullmac_he.c` - 802.11ax HE (High Efficiency) feature support
  - OFDMA (Orthogonal Frequency Division Multiple Access)
  - TWT (Target Wake Time) integration
  - Multi-user capabilities
- Firmware loading mechanism (secure blob verification per regulatory requirements)
- Command/response queue management
- Interrupt handling & DMA descriptor rings
- Channel management & regulatory compliance (802.11d/h)
- HE capability advertisement & negotiation

**Architecture:**
```
wifi_fullmac_device_t
├─ bus_type (PCIe, SDIO, USB)
├─ fw_loader
├─ cmd_queue
├─ rx_ring / tx_ring
├─ irq_handler
└─ state_machine (DOWN → FIRMWARE_READY → SCANNING → CONNECTED)
```

**New Files:**
```
kernel/drivers/
├─ wifi_fullmac.h           (new - FullMAC abstraction)
├─ wifi_fullmac_pcie.c      (new - PCIe host adapter)
├─ wifi_fullmac_sdio.c      (new - SDIO host adapter)
├─ wifi_fw_loader.h         (new - firmware management)
├─ wifi_fw_loader.c         (new - firmware upload)
├─ wifi_dma_ring.h          (new - descriptor rings)
└─ wifi_dma_ring.c          (new - DMA/queue management)
```

**Milestones:**
1. PCIe device discovery & BAR mapping
2. Firmware loading & initialization
3. Command/event queues
4. Scan & association flow
5. Full WPA2/WPA3 support
6. Performance optimization

---

### Phase 5: SoftMAC WiFi Card Support (Future, Post-4.3.0)
**Note:** Deferred to 4.4.0 or later — significantly higher complexity

**What's Involved:**
- 802.11 MAC layer implementation (manage beacons, probes, auth frames)
- Rate control logic
- Power-save behavior
- Retransmission policies
- Encryption at driver level

**Estimated Effort:** 10-12 weeks + ongoing maintenance

---

## External Dependency Reduction Strategy

### Current External Dependencies (to eliminate)
1. **WiFi Firmware Blobs** → Managed locally via `wifi_fw_loader`
2. **WPA Supplicant (wpa_supplicant)** → Local `wifi_supplicant.c` (Phases 3+)
3. **cfg80211 / mac80211** → Replicate key abstractions in Flinstone
4. **libnl / libnl-genl** → Not needed; Flinstone manages this internally

### Strategy
- **Phase 1-2:** Coprocessor handles most MAC logic; Flinstone just orchestrates
- **Phase 3:** Implement local supplicant for key negotiation
- **Phase 4:** Native FullMAC driver owns most security logic
- **Fallback:** Continue using `net_wifi_host_linux.c` for Linux host testing

### Dependency Audit
Track all `#include <...>` and `#ifdef CONFIG_*` external refs:
```c
// BAD (to eliminate)
#include <openssl/aes.h>
#include <openssl/sha.h>

// GOOD (keep/inline)
#include "kernel/core/net/net_wifi_crypto.h"
```

Run audit via:
```bash
grep -r "#include <" kernel/drivers/wifi_*.c | grep -v "kernel/include"
grep -r "CONFIG_EXTERNAL" kernel/drivers/wifi_*.c
```

---

## Integration with Existing Infrastructure

### Leverage These Existing P3 Modules (Issue #279 / PR #306)
| Module | Purpose | Status | 802.11ax Relevance |
|--------|---------|--------|-------------------|
| `contract_p3_wifi.h` | WiFi contract types (frozen) | ✅ Complete | Defines `fl_net_wifi_he_cap_t`, TWT params |
| `net_wifi_he.c` | HE (High Efficiency) IE parsing | ✅ Complete | Decodes HE Capabilities/Operation from 802.11ax beacons |
| `net_netdev.h` | Network device abstraction | Use as-is | Backing for WiFi netdev |
| `net_iface.c` | Interface management | Integrate with | Register WiFi iface |
| `net_wifi_crypto.c` | AES-CCMP/GCMP encryption | Reuse for Phase 3+ | RFC 7748 key wrapping for WPA3 |
| `net_wifi_wpa.c` | WPA/WPA2 4-way handshake | Extend (lab) | Bridge to real hardware in Phase 3+ |
| `net_wifi_sae.c` | SAE (WPA3) Dragonfly KDF | Extend (lab) | RFC 7664 implementation; Phase 3 connects to driver |
| `net_wifi_twt.c` | Target Wake Time negotiation | Extend (lab) | 802.11ax power-save feature |
| `net_wifi_station.c` | Station FSM | Hook driver into | Driver implements low-level state ops |
| `net_wifi_mgmt.c` | 802.11 mgmt frame handling | Integrate with driver | Probe/Auth/Assoc frame building |
| `net_dhcp.c` | DHCP client | Use over WiFi netdev | Existing; works on any netdev (Phase 2+) |
| `net_socket.c` | Socket layer | Validate over WiFi | Verify UDP/TCP over WiFi (Phase 2+) |
| `net_wifi_db.c` | WiFi credentials DB | Already exists | Shell `wifi` command storage |

### Driver-to-Network Glue
```c
// In wifi_coprocessor.c (Phase 1)
static const netdev_ops_t wifi_coproc_ops = {
    .open    = wifi_coproc_open,     // Load FW, init
    .close   = wifi_coproc_close,    // Shutdown
    .transmit = wifi_coproc_tx,      // Send frame to coprocessor
    .poll    = wifi_coproc_poll      // Check RX, state changes
};

netdev_t *netdev = netdev_create("wlan0", NETDEV_TYPE_WIFI, &wifi_coproc_ops);
```

---

## Testing Strategy

### Unit Tests (to add)
```
tests/
├─ wifi_coprocessor_test.c      (Phase 1)
├─ wifi_uart_transport_test.c   (Phase 1)
├─ wifi_supplicant_test.c       (Phase 3)
├─ wifi_fullmac_pcie_test.c     (Phase 4)
└─ wifi_integration_test.c      (All phases)
```

### Integration Tests (existing `BPForbes_Flinstone_Tests.c`)
- Coprocessor scan & join
- Open network association
- DHCP over WiFi
- WPA2 connection flow
- TCP/UDP over WiFi

### Hardware Testing Targets
- **Phase 1:** QEMU + virtual UART, real ESP32 on development board
- **Phase 2:** Real WiFi network (open)
- **Phase 3:** WPA2/WPA3 networks
- **Phase 4:** Real PCIe WiFi card (when hardware available)

---

## File Structure Reference

### New Files (v4.3.0)
```
kernel/drivers/
├─ wifi_coprocessor.h
├─ wifi_coprocessor.c
├─ wifi_uart_transport.h
├─ wifi_uart_transport.c
├─ wifi_supplicant.h              (Phase 3)
├─ wifi_supplicant.c              (Phase 3)
├─ wifi_fullmac.h                 (Phase 4)
├─ wifi_fullmac_pcie.c            (Phase 4)
├─ wifi_dma_ring.h                (Phase 4)
└─ wifi_dma_ring.c                (Phase 4)

kernel/include/fl/
├─ wifi_coprocessor.h
├─ wifi_driver.h
└─ wifi_types.h
```

### Modified Files
- `kernel/drivers/Makefile` — Add WiFi driver compilation
- `kernel/core/net/net_netdev.c` — Register coprocessor netdev
- `kernel/core/net/net_wifi_station.c` — Hook driver state machine
- `kernel/core/net/net_wifi_mgmt.c` — Integrate driver management

---

## Success Criteria

### By End of Phase 1
- [ ] Coprocessor abstraction compiles & links
- [ ] UART transport connects to virtual ESP32
- [ ] WiFi scan command executes
- [ ] SSIDs discovered & logged

### By End of Phase 2
- [ ] Open network join succeeds
- [ ] DHCP lease acquired
- [ ] Ping works over WiFi

### By End of Phase 3
- [ ] WPA2-PSK network join succeeds
- [ ] Encrypted traffic flows
- [ ] Real-world network tested

### Full v4.3.0 Release (Issue #279 Unblocked)
- [ ] Phases 1-3 complete & tested
- [ ] No external WiFi library dependencies (except optional firmware blobs)
- [ ] Documentation of driver architecture
- [ ] Migration guide for existing WiFi code
- [ ] **802.11ax (WiFi 6) ready:** HE capability discovery & negotiation
  - HE Capabilities IE parsing (reuse `net_wifi_he.c`)
  - TWT setup/teardown over driver (integrate `net_wifi_twt.c`)
- [ ] **P3-10 criteria 20-21 unblocked:** WPA3-SAE & WPA2-PSK production-ready
- [ ] **P3-10 criterion 25 production-ready:** DHCP + UDP over WiFi netdev

---

## Known Constraints & Risks

| Constraint | Impact | Mitigation |
|-----------|--------|-----------|
| No PCIe driver experience | Phase 4 may slip | Start Phase 4 early; consider reference impl from Linux driver |
| Firmware blob licensing | Can't redistribute | Document sources; provide loader framework |
| Real WiFi hardware limited | Testing bottleneck | Prioritize simulation in QEMU; borrow hardware for Phase 4 |
| WPA3 spec complexity | Phase 3 might extend | Reuse existing `net_wifi_sae.c` heavily |

---

## Related Documentation

### Flinstone Project
- `CLAUDE.md` — Project-wide conventions
- `docs/ROADMAP.md` — P0-P9 phased work tracking
- `docs/P3_NETWORKING.md` — P3 networking foundation (IPv4, DHCP, DNS)
- `docs/P3_NETWORKING_DEFERRED.md` — P3-10 WiFi deferred items (lab vs. production)
- `docs/GITHUB_ISSUE_SYNC_279.md` — Issue #279 (P3-10 WiFi station) prerequisites & criteria
- `docs/GITHUB_ISSUE_SYNC_PR301.md` — PR #306 / commit history for WiFi lab work
- `kernel/core/net/README.md` — Existing network stack
- `contracts/networking/contract_p3_wifi.h` — P3 WiFi contract (frozen, v4.2.0)
- `Makefile` — Build configuration

### External References
- **IEEE Standards:**
  - 802.11ax-2021 (WiFi 6 / HE specification)
  - 802.11i (WPA/WPA2/WPA3 security)
  - 802.11ac (802.11ac for reference; Phase 4 targets 802.11ax)
- **RFCs:**
  - RFC 7664 (WPA3-SAE Dragonfly)
  - RFC 7748 (Elliptic Curves for Security)
- **Linux Wireless:** https://wireless.docs.kernel.org
- **ESP32 AT Command Set:** https://docs.espressif.com/projects/esp-at/en/latest/
- **QEMU 802.11 support:** (coordinate with QEMU maintainers for real 802.11ax simulation)

---

## Version History

**v4.3.0 (Planning)**
- Target: Q2-Q3 2026
- Phases 1-3 in scope
- Phase 4 contingent on available time

**v4.4.0 (Future)**
- Phase 4 continuation (FullMAC drivers)
- Phase 5 exploration (SoftMAC)


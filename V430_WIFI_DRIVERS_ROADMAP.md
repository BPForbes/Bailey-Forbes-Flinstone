# v4.3.0: WiFi Drivers & External Dependency Reduction

**Release Focus:** P4 (Drivers) - WiFi driver development with emphasis on reducing external library dependencies

**Branch:** `v4.3.0-wifi-drivers`  
**Base:** `develop`  
**Target Version:** 4.3.0

---

## Executive Summary

v4.3.0 focuses on building native WiFi driver support within Flinstone, transitioning from external WiFi libraries to self-contained driver implementations. This release prioritizes a phased approach starting with platform abstraction, moving through chipset-specific drivers, and ultimately building toward PCIe WiFi support.

**Key Goals:**
- Establish unified network device abstraction (already have `netdev.h`)
- Implement modular WiFi coprocessor support (ESP32/ESP8266 over UART/SPI)
- Reduce external library dependencies in WiFi path
- Build foundation for future FullMAC and SoftMAC drivers
- Create clean driver/firmware loading interfaces

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

### Phase 2: Open Network Support & DHCP Integration
**Rationale:** Validate full end-to-end path before adding security  
**Est. Effort:** 2-3 weeks

**Deliverables:**
- Support open (no encryption) network scanning
- Join flow without WPA
- DHCP handshake over WiFi
- Ping & UDP communication
- Network diagnostics tools

**Integration Points:**
- `net_dhcp.c` - already exists, integrate with WiFi netdev
- `net_ping_host.c` - test over WiFi interface
- `net_socket.c` - validate socket operations over WiFi

**Milestones:**
1. WiFi scan → SSID discovery
2. Open network join
3. DHCP lease acquisition
4. End-to-end connectivity test

---

### Phase 3: WPA2/WPA3 & Encryption Support
**Rationale:** Security required for real-world usage  
**Est. Effort:** 3-4 weeks

**Existing Components (leverage):**
- `net_wifi_crypto.c` - AES-CCMP/GCMP
- `net_wifi_wpa.c` - WPA handshake
- `net_wifi_sae.c` - SAE (WPA3)

**Deliverables:**
- 4-way handshake state machine
- Key derivation (PMK → PTK → GTK)
- Replay counter validation
- Re-keying support
- Integration with coprocessor or native MAC

**New Files:**
```
kernel/drivers/
├─ wifi_supplicant.h        (new - local supplicant)
├─ wifi_supplicant.c        (new - handshake orchestration)
└─ wifi_key_mgmt.c          (new - key material handling)
```

**Milestones:**
1. WPA2-PSK (Pre-Shared Key) support
2. WPA3-SAE (Simultaneous Authentication of Equals)
3. Key rotation & replay protection
4. Real-world network testing

---

### Phase 4: FullMAC WiFi Chipset (e.g., Broadcom, Qualcomm)
**Rationale:** More sophisticated hardware with better performance  
**Est. Effort:** 6-8 weeks

**Example Target: Broadcom BCM43xx (PCIe) or BCM4330 (SDIO)**

**Deliverables:**
- `kernel/drivers/wifi_fullmac_pcie.c` - PCIe transport
- `kernel/drivers/wifi_fullmac_sdio.c` - SDIO transport (optional)
- Firmware loading mechanism
- Command/response queue management
- Interrupt handling & DMA descriptor rings
- Channel management & regulatory compliance

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

### Leverage These Existing Modules
| Module | Purpose | Status |
|--------|---------|--------|
| `net_netdev.h` | Network device abstraction | Use as-is |
| `net_iface.c` | Interface management | Integrate with |
| `net_wifi_crypto.c` | AES-CCMP/GCMP | Reuse for Phase 3+ |
| `net_wifi_wpa.c` | WPA handshake skeleton | Extend |
| `net_wifi_sae.c` | SAE (WPA3) | Extend |
| `net_wifi_station.c` | Station state machine | Hook driver into |
| `net_wifi_mgmt.c` | Management frames | Integrate with driver |
| `net_dhcp.c` | DHCP client | Use over WiFi netdev |
| `net_socket.c` | Socket layer | Validate over WiFi |

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

### Full v4.3.0 Release
- [ ] Phases 1-3 complete & tested
- [ ] No external WiFi library dependencies (except optional firmware blobs)
- [ ] Documentation of driver architecture
- [ ] Migration guide for existing WiFi code

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
- `CLAUDE.md` — Project-wide conventions
- `kernel/core/net/README.md` — Existing network stack
- `Makefile` — Build configuration
- Linux Wireless Docs: https://wireless.docs.kernel.org

---

## Version History

**v4.3.0 (Planning)**
- Target: Q2-Q3 2026
- Phases 1-3 in scope
- Phase 4 contingent on available time

**v4.4.0 (Future)**
- Phase 4 continuation (FullMAC drivers)
- Phase 5 exploration (SoftMAC)


# Phase 1: WiFi Coprocessor Driver (ESP32/ESP8266 over UART)

**Status:** Initial scaffolding complete  
**Target:** v4.3.0  
**Effort Estimate:** 3-4 weeks

---

## Overview

Phase 1 establishes WiFi capability through a coprocessor module (ESP32/ESP8266) communicating over UART. This approach is chosen because:

1. **Low complexity** — Firmware handles most WiFi/MAC logic
2. **Existing hardware** — UART transport already available
3. **Rapid iteration** — Can test networking stack immediately
4. **Foundation** — Proves netdev abstraction; later phases build on it

## Architecture

```
┌─────────────────────────────────────────┐
│      Flinstone Kernel                   │
├─────────────────────────────────────────┤
│  TCP/IP Stack (existing)                │
│  ├─ IPv4, ARP, ICMP, UDP, TCP          │
│  └─ DHCP, DNS                           │
├─────────────────────────────────────────┤
│  Network Interface Layer                │
│  └─ netdev: open(), close(), tx(), poll()
├─────────────────────────────────────────┤
│  WiFi Coprocessor Bridge (NEW)          │
│  ├─ wifi_coprocessor.c/h               │
│  ├─ wifi_uart_transport.c/h             │
│  └─ AT command handling                 │
├─────────────────────────────────────────┤
│  UART Transport Layer (existing)        │
├─────────────────────────────────────────┤
│         UART Hardware                   │
└─────────────────────────────────────────┘
        │
        └──→ ESP32/ESP8266 (WiFi MAC, firmware)
```

## Implementation Tasks

### Task 1: Coprocessor Abstraction (Done)
✅ `wifi_coprocessor.h` — Interface definition  
✅ `wifi_coprocessor.c` — Core implementation  
- Device lifecycle (create, destroy)
- netdev registration (open, close, transmit, poll)
- Status tracking & queries

**Next:** Stub implementation compiles. Missing error checking.

### Task 2: UART Transport Layer (Done)
✅ `wifi_uart_transport.h` — UART interface  
✅ `wifi_uart_transport.c` — AT command bridge  
- Low-level UART I/O (send_raw, receive_raw)
- AT command framing
- Response parsing
- Coprocessor-specific operations

**Next:** Platform-specific UART driver integration needed.

### Task 3: Platform Integration (TODO)
**Estimate:** 3-5 days

Integrate with Flinstone's existing UART infrastructure:
- [ ] Link `wifi_uart_transport.c` to real UART driver
- [ ] Implement platform-specific read/write for UART
- [ ] Add timeout mechanisms for command/response
- [ ] Test with mock UART/TTY device

**Files to modify:**
- `kernel/drivers/Makefile` — Add wifi_coprocessor.o, wifi_uart_transport.o
- Reference existing UART driver (likely in `kernel/drivers/` or `kernel/arch/`)

### Task 4: Basic AT Command Support (TODO)
**Estimate:** 4-7 days

Implement AT command set for ESP32:

| Command | Purpose | Status |
|---------|---------|--------|
| `AT` | Test connection | Stubbed |
| `AT+CWMODE=1` | Set Station mode | Stubbed |
| `AT+CWLAP` | Scan WiFi networks | Stubbed |
| `AT+CWJAP=SSID,PSK` | Join network | Stubbed |
| `AT+CWQAP` | Disconnect | Stubbed |
| `AT+CIPSTART` | Open TCP socket | TODO |
| `AT+CIPSEND` | Send data | TODO |
| `AT+CIPRECVDATA` | Receive data | TODO |

**Files to update:**
- `wifi_uart_transport.c` — Parse AT responses properly
- Add response buffer handling (multi-line responses)
- Add timeout/retry logic

### Task 5: Integration with Existing netdev (TODO)
**Estimate:** 3-5 days

Wire coprocessor into Flinstone's network stack:
- [ ] Register WiFi netdev in kernel initialization
- [ ] Hook netdev_ops callbacks (open, close, tx, poll)
- [ ] Test ARP/IP traffic over WiFi interface
- [ ] Verify DHCP client works with WiFi netdev

**Files to modify:**
- `kernel/core/net/net_netdev.c` — Register coprocessor
- Possibly `kernel/core/net/net_iface.c` — Manage WiFi interface
- Build system to link new drivers

### Task 6: Testing & Validation (TODO)
**Estimate:** 4-6 days

Establish test suite:
- [ ] Unit tests for coprocessor lifecycle
- [ ] UART transport mocking/stubbing
- [ ] AT command response parsing tests
- [ ] Integration test: scan → join → DHCP → ping

**Test files to create:**
```
tests/
├─ wifi_coprocessor_test.c
├─ wifi_uart_transport_test.c
└─ wifi_integration_test.c
```

---

## Current State

### Files Created
- `kernel/drivers/wifi_coprocessor.h` — Abstraction interface
- `kernel/drivers/wifi_coprocessor.c` — Core device management
- `kernel/drivers/wifi_uart_transport.h` — UART layer interface
- `kernel/drivers/wifi_uart_transport.c` — UART AT command bridge

### What Works
- Coprocessor device creation/destruction
- netdev ops registration (no-op implementations)
- Status enum & string conversions
- Skeleton AT command functions (compile but don't execute)

### What's Stubbed / TODO
- **Platform I/O:** UART read/write must call actual UART driver
- **Response parsing:** AT responses need proper line buffering
- **Packet transport:** Send/receive over AT commands not implemented
- **Error handling:** Most error cases just return -1
- **Timeouts:** No actual timeout mechanisms
- **Build integration:** Not yet linked into Makefile

---

## Development Workflow

### 1. Set Up Fake UART for Testing
Create a mock UART device or loop device for testing without real hardware:
```bash
# Option A: Named pipe
mkfifo /tmp/uart_mock
nc -l /tmp/uart_mock

# Option B: Virtual TTY
socat -d -d PTY,raw,echo=0 PTY,raw,echo=0
# Links two virtual TTYs for loopback testing
```

### 2. Test AT Commands Manually
```bash
# Send AT commands to mock UART
echo "AT" > /dev/ttyUSB0
# Read response
cat /dev/ttyUSB0
```

### 3. Build & Debug
```bash
cd Bailey-Forbes-Flinstone
make clean
make

# Run tests
./test_wifi_coprocessor
```

### 4. Validate with Real ESP32
Once code compiles:
1. Connect ESP32 via USB (shows up as /dev/ttyUSB0 or similar)
2. Flash ESP AT firmware if not already installed
3. Run Flinstone and check dmesg for coprocessor initialization

---

## Integration Points

### Existing Code to Reuse
| Module | Purpose |
|--------|---------|
| `net_netdev_t` | Network device abstraction |
| `netdev_ops_t` | Device operations callbacks |
| `netdev_create()`, `netdev_destroy()` | Device lifecycle |
| UART driver (TBD) | Hardware serial I/O |

### New Code Dependencies
- `wifi_coprocessor.c` ← depends on `net_netdev.h`
- `wifi_uart_transport.c` ← depends on `wifi_coprocessor.h` + UART driver

---

## Known Limitations & Caveats

1. **No encryption yet** — Phase 1 only supports open networks
2. **AT command only** — No raw WiFi frame access (coprocessor handles this)
3. **Single coprocessor** — Supports one WiFi module (expandable later)
4. **No power management** — Coprocessor always active (TODO in Phase 2)
5. **Blocking I/O** — No async/interrupt-driven RX yet
6. **Serial errors** — No automatic recovery or framing error detection

---

## Next Phases (Preview)

### Phase 2: Open Network + DHCP
- Remove WPA requirement
- Full DHCP integration
- Test end-to-end connectivity

### Phase 3: WPA2/WPA3
- Implement local supplicant (4-way handshake)
- Key derivation & encryption
- Real-world network testing

### Phase 4: FullMAC Driver
- Move to native PCIe/SDIO WiFi chipset
- Firmware loading
- DMA rings, interrupts
- Significantly more complex

---

## References
- ESP32 AT Instruction Set: https://docs.espressif.com/projects/esp-at/en/release-v2.4.0.0_esp32/AT_Command_Set/
- Flinstone Networking: `kernel/core/net/README.md`
- Device Driver Architecture: `kernel/drivers/drivers.h`


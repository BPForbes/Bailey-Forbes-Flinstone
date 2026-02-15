# VM Port Ranges (Current Baseline)

## IDE (0x1F0–0x1F7)

| Port | R/W | Purpose |
|------|-----|---------|
| 0x1F0 | R/W | Data (512-byte sector, byte stream) |
| 0x1F1 | R | Error/feature (read: 0) |
| 0x1F2 | R | Sector count (read: 1) |
| 0x1F3 | W | LBA 0–7 |
| 0x1F4 | W | LBA 8–15 |
| 0x1F5 | W | LBA 16–23 |
| 0x1F7 | R | Status (0x40 = ready) |

Backend: block_driver (host: disk file; bare-metal: IDE).

## PIT (0x40–0x43)

| Port | R/W | Purpose |
|------|-----|---------|
| 0x40 | R | Channel 0 (low byte of tick count) |
| 0x43 | W | Mode (stored, not emulated) |

Backend: timer_driver->tick_count() (host: incrementing counter).

## PIC (0x20, 0x21, 0xA0, 0xA1)

| Port | R/W | Purpose |
|------|-----|---------|
| 0x20 | W | EOI (master) |
| 0xA0 | W | EOI (slave) |
| 0x21, 0xA1 | R | Mask (read: 0xFF) |

Backend: pic_driver->eoi() when writing to 0x20/0xA0.

## Keyboard (0x60, 0x64)

| Port | R/W | Purpose |
|------|-----|---------|
| 0x60 | R | Scancode (host kbd queue or keyboard_driver) |
| 0x64 | R | Status (0) |

## Serial (0x3F8, 0xF8)

| Port | R/W | Purpose |
|------|-----|---------|
| 0x3F8, 0xF8 | W | Output byte to stdout |

# Milestone: Improve-Sys-Architecture

Branch: `milestone/Improve-Sys-Architecture`
Base:    `develop` @ 849e547

---

## Goal

Move the kernel from a functional simulator to a system that would survive
contact with real hardware.  Three layers of work:

1. **Bug fixes** — things that are outright wrong and would crash or
   misbehave on real hardware today.
2. **Realism gaps** — subsystems that exist but are stubbed out in ways that
   prevent real operation.
3. **Architecture improvements** — structural issues that make the system
   fragile, non-reentrant, or unable to grow.

---

## Section 1 — Critical Bug Fixes

### 1.1  ARM GIC EOI ignores the IRQ number
**File:** `kernel/arch/aarch64/hal/arm_gic.c:32`

```c
/* BUG: hardcoded 1023 — should be the actual irq parameter */
fl_mmio_write32((void *)((char *)c + GICC_EOIR), 1023);
```

Fix: pass `irq` to the EOI register.  Without this every interrupt
acknowledgment goes to the wrong vector and the GIC will never de-assert any
real interrupt line.

---

### 1.2  x86_64 timer tick counter never increments
**File:** `kernel/drivers/timer_driver.c`

`hw_tick_count()` returns `impl->ticks` which nothing ever writes.  The PIT
is programmed at init time (good) but there is no IRQ0 handler to increment
the counter.  Any code that calls `tick_count()` twice and expects the values
to differ will spin forever.

Fix: add a minimal IRQ0 C handler (`timer_irq0_handler`) that increments
`impl->ticks`, register it through the IRQ subsystem, and set up the IDT
entry for vector 0x20 on x86_64.  The ISR must send EOI to the PIC before
returning.

---

### 1.3  PS/2 keyboard treats raw scan codes as ASCII
**File:** `kernel/drivers/keyboard_driver.c:59`

```c
*out = (sc < 128) ? (char)sc : '\0';
```

Port 0x60 delivers PS/2 Set-1 make/break scancode values, not ASCII.  For
example, the `a` make code is `0x1E`, while ASCII `a` is `0x61`, so direct
casting with `(sc < 128) ? (char)sc : '\0'` produced largely incorrect
characters for most keys, including modifiers, function keys, arrows, and
break codes.

Fix: add a US-QWERTY Set-1 scancode-to-keymap lookup (unshifted + shifted),
track make/break and modifier state (Shift, Caps Lock), and only emit a
character on printable make codes.  Ignore break codes (bit 7 set) and
non-printable make codes instead of direct-casting raw scancodes.

---

## Section 2 — Realism Gaps

### 2.1  Memory domains provide no isolation
**File:** `kernel/core/mm/mem_domain.c`

All eight domains call straight through to `malloc`/`free` — the domain
parameter is discarded.  The API exists but protects nothing.

Fix: back each domain with a fixed-size arena (static `uint8_t` arrays in a
BSS section, sizes tuned per domain).  The arena uses a bump pointer for
allocation and a free-list for reclaim.  This makes per-domain exhaustion
visible and prevents one subsystem from silently consuming another's memory.
Implementation must remain libc-free in `DRIVERS_BAREMETAL` builds.

Proposed domain sizes:
| Domain        | Arena size |
|---------------|-----------|
| MEM_DOMAIN_FS | 128 KB    |
| MEM_DOMAIN_MM | 64 KB     |
| MEM_DOMAIN_DRV| 32 KB     |
| MEM_DOMAIN_NET| 32 KB     |
| others        | 16 KB each|

---

### 2.2  VGA hardware cursor not positioned
**File:** `kernel/drivers/display_driver.c`

Text is written to the VGA framebuffer correctly but the blinking hardware
cursor (driven by the CRT controller at ports 0x3D4/0x3D5) is never updated.
On real hardware the cursor stays at row 0, col 0 while text appears at
whatever position the software writes to.

Fix: after every `putchar` call, write the new cursor position to CRT
registers 0x0E (high byte) and 0x0F (low byte) via `fl_ioport_out8`.  Must
be conditional on `DRIVERS_BAREMETAL` and `__x86_64__`/`__i386__`.

---

### 2.3  devfs only supports sector-aligned reads/writes
**File:** `kernel/drivers/driver_model.c:232–251`
**Header:** `kernel/include/fl/driver/devfs.h`

The devfs read/write interface takes a `unit` (sector index) and transfers
exactly one sector.  Block devices are correctly unit-based, but character
devices (UART, keyboard) need arbitrary-length byte transfers.

Fix: extend `fl_devfs_ops_t` with a second read/write pair that takes a byte
`offset` and `length`.  Block devices implement the sector pair; character
devices implement the byte pair.  Neither pair is required — a device that
only fills in one is still valid.

---

### 2.4  Driver model is non-reentrant
**File:** `kernel/drivers/driver_model.c` (throughout)

All device registration, probe, and IRQ dispatch operate on global static
arrays with no locking.  On a multi-core or interrupt-driven system any
concurrent access produces races.

Fix: add a single `volatile int g_drv_lock` spinlock (test-and-set via GAS
`lock bts` on x86, `ldxr`/`stxr` on ARM) that guards the three static
arrays.  IRQ dispatch acquires then releases.  Registration saves/restores
interrupt state around the critical section.  Keep the spinlock in a separate
`kernel/core/sys/spinlock.s` per the project's ASM-for-primitives rule.

---

### 2.5  Static device/driver/IRQ array limits
**File:** `kernel/drivers/driver_model.c:7–12`

```c
#define FL_MAX_DEVICES  16
#define FL_MAX_DRIVERS  16
#define FL_MAX_IRQS     32
```

16 devices is tight once keyboard + display + block + timer + PIC + PCI bus
are registered.  The limit is not enforced with a visible error — the code
silently does nothing when the table is full.

Fix: raise limits to 32/32/64 and add a `kpanic`-style assertion when
registration exceeds the limit so overflows are immediately visible.

---

## Section 3 — Architecture Improvements

### 3.1  No IDT / exception vector table
**Both architectures**

There is no interrupt descriptor table on x86_64 and no exception vector
table on AArch64.  Without these, hardware exceptions (page fault, divide by
zero, undefined instruction) and IRQs deliver to unknown addresses and the
CPU resets or triple-faults.

Fix (x86_64): add `kernel/arch/x86_64/boot/idt.s` — 256-entry IDT in `.data`,
a generic stub that pushes a vector number and calls a C dispatcher, and a
`idt_install` function that loads the IDT with `lidt`.  Wire the PIC IRQ
lines (vectors 0x20–0x2F) to the dispatcher.  Call `idt_install()` from the
driver init path.

Fix (AArch64): add `kernel/arch/aarch64/boot/vectors.s` — the 2 KB-aligned
exception vector table required by AArch64 (four levels × four kinds = 16
entries, each 32 instructions).  Stub handlers branch to a C dispatcher.
Install with `msr vbar_el1, x0`.

---

### 3.2  No GDT / privilege separation on x86_64
**File:** `kernel/arch/x86_64/` (missing)

Without a GDT, the CPU runs with whatever segment descriptors the bootloader
left.  Port I/O and MMIO work only because most bootloaders leave flat
segments, but this is not guaranteed.

Fix: add `kernel/arch/x86_64/boot/gdt.s` with a minimal flat GDT (null,
kernel code CS=0x08, kernel data DS=0x10) and a `gdt_install` function using
`lgdt` + far return to reload CS.  Call before `idt_install`.

---

### 3.3  kmalloc has no physical page frame awareness
**File:** `kernel/core/mm/kmalloc.c`

`alloc_page()` uses `aligned_alloc(4096, 4096)` which gives virtual memory
from the libc heap, not a tracked physical frame.  A real kernel needs to
know which physical pages are in use.

Fix: add a minimal physical frame allocator (`kernel/core/mm/pmm.c`):
a static bitfield over a declared `PHYS_MEM_SIZE` (4 MB for now, matching the
ramdisk size), with `pmm_alloc_frame()` / `pmm_free_frame()` returning
physical addresses.  `alloc_page()` routes through `pmm_alloc_frame()` in
`DRIVERS_BAREMETAL` builds; host builds keep `aligned_alloc`.

---

## Execution Order

Items are ordered so that each depends only on things before it.

| # | Item | File(s) touched | Effort |
|---|------|-----------------|--------|
| 1 | Fix ARM GIC EOI | arm_gic.c | Trivial |
| 2 | Fix VGA cursor | display_driver.c | Small |
| 3 | Fix device/IRQ array limits + panic on overflow | driver_model.c | Small |
| 4 | PS/2 scancode table + modifier tracking | keyboard_driver.c | Medium |
| 5 | devfs byte I/O interface | driver_model.c, devfs.h | Medium |
| 6 | Memory domain arenas (libc-free) | mem_domain.c | Medium |
| 7 | Spinlock primitive (ASM) + driver model locking | spinlock.s, driver_model.c | Medium |
| 8 | x86_64 GDT | gdt.s | Medium |
| 9 | x86_64 IDT + IRQ0 timer handler | idt.s, timer_driver.c | Large |
| 10 | AArch64 exception vector table | vectors.s | Large |
| 11 | Physical frame allocator (PMM) | pmm.c | Large |

---

## Out of Scope for This Milestone

- DMA / AHCI / NVMe block drivers
- Virtual memory / paging (depends on PMM being solid first)
- Multi-core SMP
- Network stack
- UEFI framebuffer (GOP) support

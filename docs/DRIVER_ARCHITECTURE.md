# Unified Driver Architecture

One driver API, many backends. Drivers are consistent across x86-64, ARM64, and VM.

## Unified Drivers (`kernel/drivers/`)

| Driver | Path | Uses HAL |
|--------|------|----------|
| Block | `kernel/drivers/block/block_driver.c` | fl_hal_block_transport |
| Block transport (host) | `kernel/drivers/block/block_transport_host.c` | disk_asm |
| Keyboard | `kernel/drivers/keyboard_driver.c` | fl_ioport |
| Display | `kernel/drivers/display_driver.c` | fl_mmio (VGA) |
| Timer | `kernel/drivers/timer_driver.c` | fl_ioport |
| PIC | `kernel/drivers/pic_driver.c` | fl_ioport |

## Canonical Driver API (`kernel/include/fl/driver/`)

| Header | Purpose |
|--------|---------|
| `driver.h` | Lifecycle, registration, fl_driver_ops |
| `bus.h` | Device discovery, fl_device_desc, fl_bus_enumerate |
| `device.h` | fl_device_t handle |
| `irq.h` | Interrupt controller abstraction |
| `ioport.h` | Port I/O (x86; ARM stubs) |
| `mmio.h` | Memory-mapped I/O |
| `block.h` | Block driver interface + HAL transport |
| `console.h` | Display, keyboard, timer, PIC |
| `net.h` | Network (placeholder) |
| `driver_types.h` | Canonical types; legacy aliases |
| `driver_caps.h` | Capability structs |

## Platform HALs and Arch-Specific Drivers

| Platform | HAL Path | PCI | IOPort | ARM MMIO HAL |
|----------|----------|-----|--------|--------------|
| x86-64 | `kernel/arch/x86_64/hal/` | real (CF8/CFC) | real port I/O | - |
| AArch64 | `kernel/arch/aarch64/hal/` | real (ECAM) | stubs | PL011 UART, GIC, ARM timer |
| VM | `VM/hal/` | (future) | - | - |

Both architectures have real implementations. ARM uses MMIO (PL011 UART, GIC, ARM generic timer, PCI ECAM) instead of port I/O.

## Capability Reporting (Real vs Stub)

Drivers report whether they have real hardware paths or are stubs:

- **FL_CAP_REAL** – driver has real implementation; hardware access works
- **FL_CAP_STUB** – driver is placeholder; may return 0xFF, no-ops

| Driver | Host | Baremetal x86 | Baremetal ARM |
|--------|------|---------------|---------------|
| Block | REAL (disk file) | REAL | REAL |
| Keyboard | REAL (stdin) | REAL (PS/2) | REAL (PL011 UART) |
| Display | REAL (printf) | REAL (VGA) | REAL (UART) |
| Timer | REAL (usleep) | REAL (PIT) | REAL (CNTVCT) |
| PIC | REAL (no-op) | REAL (8259) | REAL (GIC) |
| PCI | stub | REAL (CF8/CFC) | REAL (ECAM) |

**API**: `keyboard_driver_caps()`, `display_driver_caps()`, `timer_driver_caps()`, `pic_driver_caps()`, `pci_get_caps()`. Block uses `get_caps()` → `flags` includes FL_CAP_REAL.

**Refuse stub**: Call `drivers_require_real_block()` or `drivers_require_real_pci()` before relying on hardware; returns -1 if stub.

**Report**: `drivers_report_caps()` logs real/stub status at init. In baremetal mode, FATAL if any driver is stub.

**Probe/Selftest**: `driver_probe_*()` returns 0 if real. `drivers_run_selftest()` runs block read, timer tick, display putchar; exits if any fail. Build gate: `make check-stubs` fails if forbidden stub markers exist.

## Bus Types

- **FL_BUS_PCI** – PCI config space (x86/ARM ECAM)
- **FL_BUS_DT** – Device Tree (ARM)
- **FL_BUS_SYNTH** – Synthetic (host disk, VM)

## Parity Rule

If a driver exists on one platform, it must at least compile on all. Run:

```bash
make parity
```

Requires `aarch64-linux-gnu-gcc` for ARM. Install: `apt install gcc-aarch64-linux-gnu`

## Migration Path

1. **Done**: Canonical headers; arch `driver_types.h` → thin wrappers
2. **Done**: Unified drivers in `kernel/drivers/`; block uses HAL transport; keyboard/display/timer/pic use fl_ioport/fl_mmio
3. **Done**: Deleted duplicate arch-specific drivers (x86_64, aarch64)
4. **Next**: `fl_driver_registry_register_all` → `fl_bus_enumerate` → probe → attach (**`fl_result_t`**: **`FL_RESULT_OK`** to claim, **`FL_RESULT_PROBE_SKIP`** to try the next driver; **`attach`** non-OK rolls back without **start**)
5. **Future**: Virtio as common device model for all platforms

## KeyQuorum security driver (ABI)

Recorded from source comments removed in PR #416 (2026-11).

The KeyQuorum driver (`kernel/drivers/keyquorum/kq_driver.c`, ops in `kernel/include/fl/driver/keyquorum.h`,
contract `contracts/drivers/contract_p4_keyquorum.h`) is the only code that calls KeyQuorum's C ABI for files and
accounts. It owns no policy. The kernel's checkpoints (`kq_checkpoint.c`) decide what each result means.
Callers get the driver through `fl_kq_driver_get()`, or `fl_kq_driver_get_or_register()` before `drivers_init`.

### Contract constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `FL_KQ_ABI_VERSION` | 7 | the files ABI the driver was written for; `_Static_assert` against `KQ_FILE_ABI_VERSION` |
| `FL_KQ_HOST_THRESHOLD` | 2 | valid signatures the host needs before it releases a letter (author and recipient) |
| `FL_KQ_RECEIVER_THRESHOLD` | 3 | conditions the recipient needs before it writes the file (`kq_file_readiness`) |
| `FL_KQ_DELIVERY_TTL_SEC` | 86400 | how long an unanswered co-signature request lives |
| `FL_KQ_RELEASE_TTL_SEC` | 86400 | how long a released letter waits for the recipient's receipt |
| `FL_KQ_LETTER_MAX` | 4 MiB | largest sealed letter the host holds (container plus envelope) |
| `FL_KQ_OFFER_WIRE_LEN` | 200 | bytes of a public offer on the wire |
| `FL_KQ_DELIVERY_ID_LEN` | 16 | delivery id length (`KQ_FILE_DELIVERY_ID_LEN`) |
| `FL_KQ_ACCOUNT_SECRET_LEN`, `_PUBLIC_LEN` | 32, 32 | account signing and box keys |
| `FL_KQ_ACCOUNT_SIGNATURE_LEN`, `_NONCE_LEN` | 64, 16 | account signature and bridge nonce |
| `FL_KQ_ACCOUNT_SEALED_LEN` | 81 | a password-sealed account secret |
| `FL_KQ_ACCOUNT_BRIDGE_REQUEST`, `_ACCEPT` | 1, 2 | bridge halves; a bridge signature names its role |
| `FL_KQ_SUDO_INVOKER`, `_AUTHORITY` | 1, 2 | roles in a co-signed elevation; `FL_KQ_SUDO_ACTION_MAX` 4096 |
| `FL_KQ_ACCESS_*` | 0 to 4 | factors `kq_access_requirement` returns (none, password, KQST and password, password with bridge KQST, refused) |
| `FL_KQ_BRIDGE_*` | 0, -1, -3, -5, -6 | proof statuses (OK, invalid, no room, refused, stale); a separate namespace from the account codes |

Every constant is checked against KeyQuorum's own header by `_Static_assert` in `kq_driver.c`. A submodule bump
that changes one fails the build. The runtime probe checks only the linked ABI version.

### Result codes

File ops return KeyQuorum's `KQ_FILE_*` status unchanged. Account ops return `KQ_ACCOUNT_*`
(`FL_KQ_ACCOUNT_OK` 0, `INVALID` -1, `NO_ROOM` -3, `WRONG_PASSWORD` -4). The caller maps `WRONG_PASSWORD`.
The driver never converts a status, so the orchestrator decides what each outcome means.

### Op table

| Group | Ops | Notes |
|-------|-----|-------|
| Files | `file_new`, `check_in`, `verify`, `head`, `payload`, `signers`, `merge`, `select_read` | the tracked-file container (`KQTF`) |
| Delivery, sender | `offer`, `seal` | the public offer and the letter sealed to the recipient |
| Delivery, host and co-signer | `offer_verify`, `cosign`, `cosign_verify` | signatures over the public offer only |
| Delivery, recipient | `open`, `attach`, `readiness`, `freshness` | `freshness` compares with the seen pairs (`FIRST`, `REPLAYED`, `NEWER`, `NOT_NEWER`) |
| Supervisor | `permits_newer`, `countersign` | `permits_newer` must return `KQ_FILE_OK` before a newer copy is accepted |
| History | `events`, `event_name`, `outcome_name`, `head_author` | display names are never parsed back |
| Snapshot (ABI 3) | `seal_snapshot`, `open_snapshot`, `snapshot_covers` | kind-17 letter; `snapshot_covers` returns `KQ_FILE_OK` or `KQ_FILE_DIVERGED` |
| Error reports (ABI 3) | `error_sign`, `error_inject`, `error_name`, `error_class_name` | `KQER` sidecars |
| Memory | `wipe`, `equal` | `wipe` is `mem_domain_secure_zero`; `equal` is constant time |
| Account keys | `account_keygen`, `account_public`, `account_box_keygen`, `account_box_public`, `account_seal`, `account_open` | fixed-size caller buffers, nothing allocated or retained |
| Bridges | `account_bridge_sign`, `account_bridge_verify`, `bridge_prove`, `bridge_verify`, `bridge_connects` | the rule is KeyQuorum's |
| Access | `access_requirement` (ABI 4) | the factor a login, `su` or `sudo` must present |
| Slots | `slot_open` (ABI 4), `slot_seal` (ABI 7) | store-free KQST open and seal |
| Assembly (ABI 6) | `assembly_threshold`, `assembly_split`, `assembly_generation`, `assembly_assemble` | hosted builds only; NULL in the freestanding image |
| Sudo (ABI 7) | `sudo_sign`, `sudo_verify` | both halves must verify |

Ops marked hosted-only are NULL in the freestanding image. A caller that reads NULL treats the operation as
refused.

### Rules for callers and for the driver

- **Buffers.** An op that produces data asks KeyQuorum for the size first, allocates, then calls again. `out` changes
  only after the second call succeeds, so an input that aliases `out` stays intact on failure. Data is released with
  `fl_kq_buf_free`, which wipes it first.
- **Secrets.** The caller wipes every secret buffer it passes or receives. The driver wipes its own copies with
  `mem_domain_secure_zero`, and digests compare with `asm_mem_ct_equal`.
- **Probe.** `fl_kq_driver_get()` returns the driver only when the registry holds `fl_kq_driver_desc()` and the ABI probe
  accepts. It returns NULL after `drivers_shutdown` and for an empty registry. It matches the registered pointer, not
  the name.
- **Account ops** share the file probe. They have no ABI symbol of their own, because they come from the same linked
  archive.
- **Freestanding image.** There is no driver model or registry. The resolver is the same ABI probe on the one static
  descriptor.

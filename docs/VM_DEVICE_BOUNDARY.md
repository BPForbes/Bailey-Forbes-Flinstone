# VM Device Boundary

All guest I/O goes through **emulated ports**; no direct host API access from guest execution.

## Rule

Guest code runs in the **execute** loop. It can only:
- Read/write guest RAM (vm_mem)
- Trigger port I/O via IN/OUT instructions

Port I/O is handled by **vm_io** only. vm_io maps ports to emulated devices:

| Port range | Device | Host implementation |
|------------|--------|---------------------|
| 0x1f0–0x1f7 | IDE | vm_disk (backed by file) |
| 0x60, 0x64 | Keyboard | vm_host kbd queue |
| 0x40–0x43 | PIT | vm_host vm_ticks |
| 0x20, 0x21, 0xA0, 0xA1 | PIC | vm_io (ack only) |
| 0x3f8, 0xf8 | Serial | stdout (host) |

## Boundary

- **Guest execution path**: vm_decode → execute → vm_io_in/out. No fopen, printf, or host FS from within instruction execution.
- **Host setup**: vm_disk, vm_loader use fopen; that is host infrastructure implementing the *emulated* devices. Guest never sees these.
- **Monitor**: vm_host_dump_registers uses fprintf; that is host tooling, not guest.

## Enforcement

- All guest-visible I/O flows through vm_io_in/vm_io_out.
- vm_io is the single point where port numbers are translated to host behavior.
- No guest-accessible handles (FILE*, etc.); only port traps.

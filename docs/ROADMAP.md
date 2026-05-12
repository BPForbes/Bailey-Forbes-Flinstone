# Flinstone OS-Platform Roadmap

This document is the **single platform roadmap** for Flinstone: phased goals (**P0–P9**), **outsider-facing gap** tables (what is still missing for a credible OS read), **normative references** (IEEE, RFC, UEFI, POSIX, Virtio, and so on), **rough implementation** sketches (typically **H → K → B**), and **Appendix D** (bare-metal correctness checklist: GIC EOI, IDT/vectors, PMM, PS/2, devfs, spinlocks, execution order). It does **not** commit to calendar dates; phases are **dependency-ordered**.

**Scope tiers (pick one primary track per initiative):**

| Tier | Meaning |
|------|--------|
| **H** | **Hosted:** shell and subsystems run as a normal process on Linux (fastest path to real I/O). |
| **K** | **Kernel-shaped:** same abstractions, stricter boundaries; still may run hosted until a boot path exists. |
| **B** | **Bare metal / VM-first:** firmware-visible ownership, guest-visible devices, or true supervisor bring-up. |

Unless stated otherwise, **start at H**, prove APIs and tests, then **lift** the same contracts toward K/B.

**Global engineering standards (apply to every item below):**

1. **Layering:** hardware-facing code stays behind narrow driver ops; policy and orchestration stay in C; hot paths may use existing ASM patterns where the repo already does so.
2. **Determinism & failure modes:** every new subsystem defines **error codes**, **OOM behavior**, and **degraded operation** (what still works when a dependency fails).
3. **Tests:** each feature lands with a **focused automated test** (existing `make` test target or new one); hardware-only paths need **QEMU/tap/offline** shims for CI.
4. **Documentation:** public contract (headers + short `docs/` section) before wide refactors; **no silent behavior change** for shell builtins.
5. **Security default:** **deny-by-default** for new powers (raw I/O, promiscuous NIC, arbitrary mount); explicit capability or build flag to enable lab modes.

---

## Phase 0 — Foundations (cross-cutting)

These items unblock almost everything else.

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P0-1** | **Subsystem boundaries** | Freeze public C APIs for “driver ops”, “netdev”, “log sink”, “auth check”. | Headers document lifetime/threads; **no circular deps** across `kernel/` ↔ `userland/` beyond existing glue; `make` parity for default arch. |
| **P0-2** | **Error taxonomy** | Shared `errno`-style or project-specific result type for drivers, VFS, net. | Every new API documents **who frees memory**; no unchecked `void` returns for fallible ops. |
| **P0-3** | **CI realism** | CI runs unit tests without special hardware; optional nightly for TAP/loopback. | **GitHub Actions** green on default matrix; flaky tests quarantined with issue link. |

---

## Phase 1 — Core runtime & process model (K/B track; partial H)

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P1-1** | **Execution context** | Define what a “task” is (thread in hosted mode; kernel thread later). | **POSIX threads** acceptable on H; document **signal-safety** rules for driver callbacks. |
| **P1-2** | **Address space story** | Document flat vs paged memory for hosted vs future kernel. | For H: **W^X policy** where applicable (`mmap` advice); for K/B: reference **MMU programming** for target arch (ARMv8A / x86-64 manuals). |
| **P1-3** | **Preemption contract** | Identify which locks may be held across which subsystems. | **Lock ordering graph** in `docs/`; no unbounded work under spinlocks; **irq-safe** rules documented even if hosted IRQ is simulated. |

**References:** POSIX.1 threads; Linux *Understanding the Linux Kernel* (scheduler/MM chapters) as conceptual guide only.

---

## Phase 2 — Identity, users, and privilege (“root” and beyond)

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P2-1** | **Principal model** | Introduce `uid`/`gid` (or capability sets) in the service layer. | **Least privilege default:** shell builtins that touch disks/network/raw I/O require elevated principal or explicit `-y` only where already idiomatic. |
| **P2-2** | **Credential store (hosted)** | Minimal `/etc`-style or in-repo config for users (even single user + root). | Password handling: **no plaintext on disk**; use existing host crypto (`crypt(3)` / libsodium) on H; document threat model (“lab only”). |
| **P2-3** | **Authorization middleware** | Central `can_*` checks before FileManager, netdev, mount. | **Unit tests** deny guest principal for privileged ops; **audit log entry** (see Phase 6) on deny/allow. |
| **P2-4** | **Sudo-like elevation (hosted)** | Time-bounded elevation token or explicit `runas` builtin. | **TTY-bound** confirmation for elevation where possible; log **who/when/why**. |

**References:** POSIX.1-2008 (uids); *Saltzer & Kaashoek* principles; avoid inventing crypto—reuse vetted libraries on H.

---

## Phase 3 — Networking (H → K; incremental RFC alignment)

Implement **bottom-up**: **L2 (link layer)** → IPv4/UDP → TCP → sockets façade.

**L2 in this roadmap** means **IEEE 802.3 Ethernet**: MAC addressing, on-the-wire **Ethernet frame** format (length/type field, FCS where applicable), and the usual **DIX / Ethernet II** encapsulation of IPv4 (**RFC 894**). Linux **TUN/TAP** exposes **Ethernet** devices as **802.3-style** frames to user space; the stack should treat **raw L2** as **802.3 Ethernet**, not as an abstract “byte pipe,” so **ARP** and IPv4 placement align with real NICs and lab TAP bridges.

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P3-1** | **Device abstraction (`netdev`)** | TX/RX **IEEE 802.3 Ethernet** frames, MAC get/set, promisc flag, stats counters. | **IEEE 802.3** frame handling (addressing, MTU vs frame size); API mirrors **Linux `net_device`** minimal subset conceptually; **zero-copy optional**, **copy-based default** for simplicity. |
| **P3-2** | **Loopback (software)** | IPv4 `127.0.0.0/8` minimal: ping self, UDP echo. | **RFC 1122** host requirements subset; tests run **without** `/dev/net/tun`. |
| **P3-3** | **TAP backend (hosted only)** | Read/write **IEEE 802.3 Ethernet** frames via Linux TUN/TAP. | **802.3 Ethernet** on the TAP device; root or `CAP_NET_ADMIN` documented; CI uses **virt** runner or skips with explicit `SKIP_TAP=1` + reason in log. |
| **P3-4** | **ARP** | IPv4 neighbor resolution over **IEEE 802.3 Ethernet**. | **RFC 826** over **802.3 MAC**; **RFC 894** type/ethertype expectations; cache with eviction bounds; **flood protection** hooks (stub OK initially). |
| **P3-5** | **IPv4** | Addresses, netmask, routing table (default route), ICMP echo (ping). | **RFC 791** + **RFC 792**; **checksum offload** optional; **path MTU** stub documented. |
| **P3-6** | **UDP** | Demux by port; checksum; basic socket buffer caps. | **RFC 768**; **bounded queues**; drop policy under pressure documented. |
| **P3-7** | **TCP (large)** | Reliable stream for one client/server pair first. | **RFC 793** + selective **RFC 5681** congestion basics later; **interop test** against Linux `nc` or `socat`. |
| **P3-8** | **DNS client** | Resolver for A/AAAA records (AAAA optional). | **RFC 1035** semantics subset; **timeouts** and **retry caps**. |
| **P3-9** | **TLS (hosted)** | Prefer **userland** TLS (e.g. mbedTLS/OpenSSL) behind shell command or libc. | **No TLS in “kernel” layer** until stable memory and time APIs exist on K/B. |

**Security standards:** default **no promisc**; **no raw TX** from shell without principal + audit; rate-limit outgoing ARP/ICMP in lab builds.

---

## Phase 4 — Drivers & hardware (K/B; staged complexity)

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P4-1** | **Driver model v2** | Registration, probe/remove, power hooks, DMA mask. | **Linux driver model** as *conceptual* reference; document differences explicitly. |
| **P4-2** | **IRQ lifecycle** | Hardirq vs threaded bottom half (or workqueue analogue). | **No sleep in true hardirq path**; lockdep-style assertions in debug builds where feasible. |
| **P4-3** | **PCIe config space access (lab)** | MMIO config for one QEMU NIC class. | **PCIe spec** excerpts in docs; **IOMMU later** milestone flagged. |
| **P4-4** | **Virtio net/block** | Paravirtual devices for VM path. | **Virtio 1.x** spec; ring format tests with **golden vectors**. |
| **P4-5** | **USB stack** | Deferred—document as **Phase 4+** (complex state machines). | If started: **xHCI subset** only; compliance tests out-of-tree. |

**Hardware policy:** each driver ships with **QEMU command line** + **known-good hardware ID** table.

---

## Phase 5 — Storage, VFS, and durability

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P5-1** | **VFS layer** | Mount table, vnode/inode abstraction, path walk cache limits. | **POSIX pathconf** subset where relevant; **ELOOP** detection on symlinks if added. |
| **P5-2** | **Pluggable FS** | ext4 read-only or FUSE-hosted bridge on H before native ext4 on B. | **fsck** story documented; **journalling** requirements tabled. |
| **P5-3** | **Page cache** | Unified buffer cache between net and block (long-term). | **POSIX fadvise** semantics optional; **coherency rules** documented. |

---

## Phase 6 — Observability & system loggers

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P6-1** | **Structured log API** | `log(level, facility, fmt, …)` with **rate limit** and **IRQ-safe variant** (no alloc in hardirq). | Levels align with **syslog(3)** severity names for familiarity. |
| **P6-2** | **Ring buffer sink** | In-memory `dmesg`-style buffer with drop counters. | **Lossless mode** optional cap; overflow behavior **tested**. |
| **P6-3** | **Persistent log (hosted)** | Append-only file under configurable dir; rotation by size. | **fsync policy** documented (performance vs durability); **secrets redaction** hook list. |
| **P6-4** | **Audit trail** | Security-relevant events (auth, mount, raw I/O). | **Append-only** store; **tamper-evidence** optional later (signed log segments). |
| **P6-5** | **Tracing hooks** | Static tracepoints for scheduler, net, block hot paths. | **DTrace-style naming** optional; **zero cost when disabled** (macros to NOP). |

**References:** syslog protocol **RFC 5424** for wire format if exporting off-box later.

---

## Phase 7 — Shell UX, ops, and packaging

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P7-1** | **Service supervision** | Start/stop/restart long-running net or log daemons (hosted). | **PID files** + stale detection; **graceful shutdown** timeouts. |
| **P7-2** | **Packaging** | Reproducible tarball/OSTree image (later). | **SBOM** manifest optional; **version** from existing `version/locked` pipeline. |
| **P7-3** | **Remote admin path** | SSH is heavy; minimum viable: **reverse shell** lab command behind compile flag. | **Never default-on**; documented abuse risks. |

---

## Phase 8 — Virtualization & guest fidelity (VM track)

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P8-1** | **Device timing fidelity** | Deterministic or bounded-time device models for tests. | **Replay tests** (`make test_replay`) extended for NIC events. |
| **P8-2** | **Guest virtio** | Align with virtio specs used in Phase 4. | **Inter-vm** ping using TAP bridge documented. |

---

## Phase 9 — Hardening, compliance, and scale

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P9-1** | **Fuzzing** | syscall / netdev / FS parsers under AFL++ or libFuzzer (hosted shims). | **Crash = bug**; corpus checked in CI cache optional. |
| **P9-2** | **Coverity / static analysis** | Clean critical triage. | **Zero** new high-severity defects per release gate. |
| **P9-3** | **SMP bring-up (B)** | IPIs, per-CPU variables, barrier rules. | **Memory model** doc for aarch64/x86 per **ARM ARM** / Intel SDM. |

---

## Platform credibility — gaps, standards, and rough implementation

The phase tables above define **goals and acceptance**. This section lists **what is still missing or immature** if the project should read as a **legitimate OS-style platform** to outsiders, with **standards to cite** and a **rough implementation path**. **Ordering** follows **P0 → P9**; use the phase IDs when scheduling work.

**Bare-metal tactical bugs** (EOI, IDT, PMM, PS/2, etc.) are tracked in **Appendix D** below. When work ships, record it under **`version/entries/*.ver`** per repository policy and keep phase rows honest.

### 1 — Foundations and rigor

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| Stable **subsystem boundaries** (driver ops, netdev, VFS, log, authz) | Project headers as contract; **POSIX.1** for hosted behavior where applicable | Freeze small C APIs in `kernel/include/` / `userland/`; document thread + lifetime rules; optional CI compile guard on forbidden includes across layers. |
| Shared **error / result taxonomy** | **errno** conventions (POSIX); optional project `fl_result_t` pattern | One typedef + macros; migrate new call sites first; tests assert stable error codes for user-visible failures. |
| **CI realism** for optional I/O (TAP, loopback) | **GitHub Actions** matrix docs; skip flags documented (**`SKIP_TAP`** style) | Default CI: unit tests; optional job on self-hosted or `virt` runner; skip prints reason to log. |

### 2 — Core runtime: processes, memory, scheduling

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| Clear **task / thread** model | **POSIX threads** on hosted **H**; **Linux scheduler** docs as conceptual only | Map shell + driver callbacks to pthreads on H; on K/B introduce `struct kthread` + run queue stub before real preempt. |
| **Address spaces** / MMU story | **ARM Architecture Reference Manual** (EL1 paging); **Intel SDM** Vol. 3A (paging) | After PMM (see **Appendix D**): page tables, `map_kernel`, user mappings optional; identity map + guard pages first. |
| **Preemption, locks, IRQ** contract | **ARM GIC** architecture spec; **Intel 8259 PIC / APIC** overview | Lock ordering in `docs/`; spinlocks in driver model; “no sleep in hardirq” checks in debug builds. |

### 3 — Identity, privilege, trust

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **Principals** (`uid`/`gid` or capabilities) | **POSIX.1-2008** (uids, supplemental groups); **Linux capabilities(7)** as model | Hosted: map to real uids or synthetic table; shell builtins check principal before disk/net/raw ops. |
| **Credential store** | **crypt(3)** / **PHC**-style KDF on H; avoid home-grown crypto | Small `passwd`-like file or host integration; document lab-only threat model. |
| **Authorization middleware** | **Saltzer–Kaashoek** principles; **NIST** lightweight threat framing (informative) | Central `fl_authz_can_*()` from VFS and netdev glue; unit tests deny guest. |
| **Elevation** (sudo-like) | **sudo** UX patterns (informative); audit severities may align with **RFC 5424** | Time-bounded token or `runas` builtin; TTY confirm; log to Phase 6 logging + audit. |

### 4 — Networking (beyond Phase 3 checklist)

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **L2 Ethernet** | **IEEE 802.3**; **RFC 894**; **RFC 826** | Same as **P3-1–P3-4**; `netdev` + TAP + ARP. |
| **IPv4 / ICMP / UDP / TCP** | **RFC 791**, **792**, **768**, **793**; **RFC 5681** (congestion, later) | Bottom-up C stack; golden pcap or `nc` interop; bounded buffers. |
| **DNS** | **RFC 1035**; **RFC 2181** (clarifications) | Resolver in userland or kernel-shaped module; timeouts + capped retries. |
| **TLS** | **RFC 8446** (TLS 1.3); **RFC 5280** (PKIX) | mbedTLS/OpenSSL on **H** first; no in-kernel TLS until K/B memory + time APIs are safe. |
| **Wi‑Fi** | **IEEE 802.11** (MAC/PHY, management); **IEEE 802.11i** (WPA2/WPA3); **IEEE 802.1X** / **EAP** (enterprise); L3 often bridged to **802.3**-style APIs | Driver + firmware (or virtio-wlan in QEMU); station state machine (scan → auth → assoc → 4-way PSK); **DHCP** (**RFC 2131**); reuse Phase 3 IPv4/TCP/UDP. |

### 5 — Drivers and hardware

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **Driver model v2** | **PCI Firmware** / **Linux device model** (conceptual) | Tables + refcount; QEMU device IDs first. |
| **IRQ lifecycle** | **GIC** / **APIC** vendor specs | Top/bottom half; EOI correctness (**Appendix D**). |
| **PCIe + MMIO** | **PCI Express Base Specification** | ECAM or legacy config; one NIC BAR; DMA + **IOMMU** later. |
| **Virtio net/block** | **Virtio 1.1+** | Rings in guest RAM; golden vectors; **Phase 8** replay for NIC. |
| **USB** | **USB 3.x / xHCI** subset | Defer; one controller class + MSD or HID if started. |

### 6 — Storage, VFS, durability

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **VFS** | **POSIX** file semantics; path resolution rules | Layer above disk/FAT glue; mount returns typed FS pointers. |
| **Second FS** (e.g. ext4 RO) | **ext4** on-disk layout (kernel.org docs); **FUSE** on H optional | RO inode walk + block cache stub; or FUSE bridge on H. |
| **Page / buffer cache** | **POSIX fadvise** (informative); BSD UBC-style reading | Unify block path with net buffers where safe; document OOM eviction. |

### 7 — Observability and operations

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **Structured logging** | **syslog(3)** names; **RFC 5424** if exporting | Same as **P6-1**; IRQ-safe variant. |
| **Ring buffer** | (project convention) | **P6-2**; overflow tests. |
| **Persistent log** | **RFC 5424** transport optional | **P6-3**; rotation; redact secrets. |
| **Audit trail** | **Common Criteria** audit concepts (informative) | **P6-4**; authz + mount + netdev. |
| **Tracepoints** | **DTrace** naming (informative) | **P6-5**; NOP when off. |

### 8 — Shell UX, packaging, “distro-shaped” behavior

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **Service supervision** | **systemd** concepts (informative); PID files | **P7-1**; stale PID handling. |
| **Images / SBOM** | **SPDX** (optional) | **P7-2**; tarball/OCI-style; `version/locked` for version string. |
| **Remote admin** | **RFC 4251** (SSH architecture, if ever building SSH); lab tunnel patterns | **P7-3**; compile-flagged; never default-on. |

### 9 — Virtualization fidelity

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **Device timing / NIC replay** | **Virtio**; internal **`make test_replay`** format | **P8-1**; netdev events in log. |
| **Inter-VM / TAP** | **IEEE 802.3** on **TUN/TAP** | **P8-2**; QEMU cmdlines; same ARP/IPv4 tests as wired path. |

### 10 — Hardening and scale

| Gap | Standards / references | Rough implementation |
|-----|------------------------|----------------------|
| **Fuzzing** | **AFL++** / **libFuzzer** | **P9-1**; hosted harnesses. |
| **Static analysis** | **MISRA C** (optional); **Coverity** / **clang-tidy** | **P9-2**; CI gate on new criticals. |
| **SMP** | **ARM ARM**; **Intel SDM** | **P9-3**; IPIs; BKL → finer locks. |

### 11 — Userland: `curl`-class and `apt`-class tools

Mostly **above** the kernel once the stack exists.

#### 11.1 HTTP client (`curl`-like)

| Need | Standards / references | Rough implementation |
|------|------------------------|----------------------|
| Name resolution | **RFC 1035**; **RFC 8484** (DoH optional, later) | Blocking resolver → async later. |
| Reliable stream | **RFC 793**; **RFC 9293** (TCP maintenance) | Connect/send/recv + timeouts. |
| TLS | **RFC 8446**; **RFC 5280** | mbedTLS/OpenSSL on H. |
| HTTP | **RFC 9110**; **RFC 9112** | Status, headers, chunked body, redirects; **HTTP/2** (**RFC 9113**, **RFC 7541**) much later. |

#### 11.2 Package manager (`apt`-like)

| Need | Standards / references | Rough implementation |
|------|------------------------|----------------------|
| Transport | Same as §11.1 | HTTPS `Release` / `Packages`. |
| Signatures | **OpenPGP** **RFC 4880**; Debian **InRelease** / **Release.gpg** (de facto) | Verify before trusting index. |
| Archives | **ar**; **tar** (**POSIX ustar**); **gzip** / **xz** | `.deb`-style unpack pipeline. |
| Dependencies | **Debian Policy** (informative) | SAT or greedy MVP; `dpkg`-like state machine later. |

### 12 — Boot and install (protocols and formats)

| Track | Standards / references | Rough implementation |
|-------|------------------------|----------------------|
| **UEFI boot** | **UEFI Specification**; **ACPI** (informative) | `.efi` PE/COFF; Boot Services; handoff to kernel/VM. |
| **Partitioning** | **UEFI** GPT; classic **MBR** (informative) | Parse GPT; find **ESP** (FAT). |
| **ESP filesystem** | **FAT32** (Microsoft spec / UEFI profile) | RO FAT path or reuse project FAT on ESP. |
| **PXE / netboot** | **RFC 2131**, **RFC 2132**, **RFC 1350** (TFTP) | DHCP → TFTP loader/kernel. |
| **HTTP(S) boot** | **UEFI** HTTP Boot | Larger than TFTP; PKIX trust as TLS. |
| **Secure Boot** | **UEFI Secure Boot**; **PKCS #7** signed PE | Chain verify; key enrollment; policy-heavy. |

---

## Appendix A — Standards map (non-exhaustive)

| Domain | Normative / de-facto references |
|--------|----------------------------------|
| C ABI / hosted behavior | ISO C11; POSIX.1-2008 where hosted. |
| Networking | **IEEE 802.3** (Ethernet L2/MAC & framing); **RFC 894** (IPv4 over Ethernet); **RFC 826** (ARP); **RFC 791**, **792**, **768**, **793**, **1035**; later TLS **RFC 5246** / **8446** via library. |
| Wi‑Fi | **IEEE 802.11**, **802.11i**; **802.1X** / **EAP**; **RFC 2131** (DHCP after link). |
| HTTP / packages | **RFC 9110**, **9112**; **RFC 8446**, **5280**; **RFC 4880** (OpenPGP); Debian archive conventions (informative). |
| Boot / firmware | **UEFI**; **RFC 2131**, **2132**, **1350** (PXE path); **PKCS #7** (Secure Boot). |
| Filesystem | POSIX file semantics; ext4 on-disk layout (kernel docs); virtio-blk 1.x. |
| Logging | RFC 5424 (transport); syslog severity names. |
| Virtio | VIRTIO 1.1+ specifications. |
| Security engineering | OWASP ASVS (for hosted services); NIST SP 800-123-style threat modeling (lightweight). |

---

## Appendix B — “Definition of done” template (copy per feature)

```text
Feature ID:
Owner:
Track (H/K/B):

Functional requirements:
- …

Non-functional requirements:
- Performance budget:
- Memory budget:
- Threading / IRQ context rules:

Tests:
- Unit:
- Integration:
- Negative / security:

Docs:
- User-facing:
- Threat model notes:

Rollout:
- Default on/off:
- Migration / compat:
```

---

## Appendix C — Suggested first vertical slice (90-day style, technical only)

1. **P3-1 + P3-2 + P6-1 + P6-2** — `netdev` + loopback IPv4/UDP + structured logging to ring buffer.  
2. **P3-3 + P3-4 + P3-5 + P3-6** — **802.3** TAP + ARP + IPv4 + UDP with shell builtins (`ping`, `udpsend`, `udplisten`).  
3. **P2-3 + P6-4** — authz middleware + audit for those builtins.  

Adjust ordering if **bare metal** becomes the primary track (move **P4*** earlier, defer **P3-3** TAP).

---

## Appendix D — Bare-metal correctness (Improve-Sys-Architecture)

The following material was merged from the deleted **`docs/milestone-Improve-Sys-Architecture.md`**. **Branch / base** metadata at the top of that content is **historical**; verify against current `develop` before relying on commit hashes.


Branch: `milestone/Improve-Sys-Architecture`
Base:    `develop` @ 849e547

---

### Goal

Move the kernel from a functional simulator to a system that would survive
contact with real hardware.  Three layers of work:

1. **Bug fixes** — things that are outright wrong and would crash or
   misbehave on real hardware today.
2. **Realism gaps** — subsystems that exist but are stubbed out in ways that
   prevent real operation.
3. **Architecture improvements** — structural issues that make the system
   fragile, non-reentrant, or unable to grow.

---

### Section 1 — Critical Bug Fixes

#### 1.1  ARM GIC EOI ignores the IRQ number
**File:** `kernel/arch/aarch64/hal/arm_gic.c:32`

```c
/* BUG: hardcoded 1023 — should be the actual irq parameter */
fl_mmio_write32((void *)((char *)c + GICC_EOIR), 1023);
```

Fix: pass `irq` to the EOI register.  Without this every interrupt
acknowledgment goes to the wrong vector and the GIC will never de-assert any
real interrupt line.

---

#### 1.2  x86_64 timer tick counter never increments
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

#### 1.3  PS/2 keyboard treats raw scan codes as ASCII
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

### Section 2 — Realism Gaps

#### 2.1  Memory domains provide no isolation
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

#### 2.2  VGA hardware cursor not positioned
**File:** `kernel/drivers/display_driver.c`

Text is written to the VGA framebuffer correctly but the blinking hardware
cursor (driven by the CRT controller at ports 0x3D4/0x3D5) is never updated.
On real hardware the cursor stays at row 0, col 0 while text appears at
whatever position the software writes to.

Fix: after every `putchar` call, write the new cursor position to CRT
registers 0x0E (high byte) and 0x0F (low byte) via `fl_ioport_out8`.  Must
be conditional on `DRIVERS_BAREMETAL` and `__x86_64__`/`__i386__`.

---

#### 2.3  devfs only supports sector-aligned reads/writes
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

#### 2.4  Driver model is non-reentrant
**File:** `kernel/drivers/driver_model.c` (throughout)

All device registration, probe, and IRQ dispatch operate on global static
arrays with no locking.  On a multi-core or interrupt-driven system any
concurrent access produces races.

Fix: add file-static spinlocks (`s_model_lock` in driver_model.c and
`s_mem_domain_lock` in mem_domain.c) that guard the static arrays.  IRQ
dispatch acquires then releases.  Registration saves/restores interrupt
state around the critical section.  The spinlock assembly primitives
(spinlock_acquire/spinlock_release) are provided by architecture-specific
boot code under `kernel/arch/{x86_64,aarch64}/boot/`: on x86_64
`kernel/arch/x86_64/boot/spinlock.s` uses `lock cmpxchgl` with a `pause`
loop (via `rep nop`), and on aarch64 `kernel/arch/aarch64/boot/spinlock.s`
uses `ldaxr`/`stxr` with `WFE`/`SEV` for efficient contention handling.

---

#### 2.5  Static device/driver/IRQ array limits
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

### Section 3 — Architecture Improvements

#### 3.1  No IDT / exception vector table
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

#### 3.2  No GDT / privilege separation on x86_64
**File:** `kernel/arch/x86_64/` (missing)

Without a GDT, the CPU runs with whatever segment descriptors the bootloader
left.  Port I/O and MMIO work only because most bootloaders leave flat
segments, but this is not guaranteed.

Fix: add `kernel/arch/x86_64/boot/gdt.s` with a minimal flat GDT (null,
kernel code CS=0x08, kernel data DS=0x10) and a `gdt_install` function using
`lgdt` + far return to reload CS.  Call before `idt_install`.

---

#### 3.3  kmalloc has no physical page frame awareness
**File:** `kernel/core/mm/kmalloc.c`

`alloc_page()` uses `aligned_alloc(4096, 4096)` which gives virtual memory
from the libc heap, not a tracked physical frame.  A real kernel needs to
know which physical pages are in use.

Fix: add a minimal physical frame allocator (`kernel/core/mm/pmm.c`):
a static bitfield over the region defined by `PMM_PHYS_MEM` (4 MB for now,
matching the ramdisk size), with physical addressing starting at
`PMM_PHYS_BASE` (1 MB), tracking `PMM_NUM_FRAMES` frames of
`PMM_FRAME_SIZE` bytes each.  The API provides `pmm_alloc_frame()` /
`pmm_free_frame()` returning physical addresses.  `alloc_page()` routes
through `pmm_alloc_frame()` in `DRIVERS_BAREMETAL` builds; host builds
keep `aligned_alloc`.

---

### Execution Order

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

### Out of Scope for This Milestone

- DMA / AHCI / NVMe block drivers
- Virtual memory / paging (depends on PMM being solid first)
- Multi-core SMP
- Network stack
- UEFI framebuffer (GOP) support

---

*Maintainers: when a roadmap item becomes committed work, record the **shipped** behavior under `version/entries/*.ver` per repository versioning policy; keep this document’s **phases**, **Platform credibility** gap tables, and **Appendix D** bare-metal checklist aligned with actual merges.*

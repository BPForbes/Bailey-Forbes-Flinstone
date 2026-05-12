# Flinstone OS-Platform Roadmap

This document is a **long-horizon engineering roadmap**: it lists major capability areas, **feature-by-feature** goals, and **implementation standards** (norms, acceptance criteria, and ordering constraints). It does **not** commit to calendar dates; phases are **dependency-ordered**.

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

Implement **bottom-up**: L2 → IPv4/UDP → TCP → sockets façade.

| ID | Feature | Goal | Standards & acceptance |
|----|---------|------|---------------------------|
| **P3-1** | **Device abstraction (`netdev`)** | TX/RX frame ops, MAC get/set, promisc flag, stats counters. | API mirrors **Linux `net_device`** minimal subset conceptually; **zero-copy optional**, **copy-based default** for simplicity. |
| **P3-2** | **Loopback (software)** | IPv4 `127.0.0.0/8` minimal: ping self, UDP echo. | **RFC 1122** host requirements subset; tests run **without** `/dev/net/tun`. |
| **P3-3** | **TAP backend (hosted only)** | Read/write Ethernet frames via Linux TUN/TAP. | Root or `CAP_NET_ADMIN` documented; CI uses **virt** runner or skips with explicit `SKIP_TAP=1` + reason in log. |
| **P3-4** | **ARP** | IPv4 neighbor resolution over Ethernet. | **RFC 826**; cache with eviction bounds; **flood protection** hooks (stub OK initially). |
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

## Appendix A — Standards map (non-exhaustive)

| Domain | Normative / de-facto references |
|--------|----------------------------------|
| C ABI / hosted behavior | ISO C11; POSIX.1-2008 where hosted. |
| Networking | RFC 791, 792, 768, 793, 826, 1035; later TLS 1.2+ **RFC 5246** / 8446 via library. |
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
2. **P3-3 + P3-4 + P3-5 + P3-6** — TAP + ARP + IPv4 + UDP with shell builtins (`ping`, `udpsend`, `udplisten`).  
3. **P2-3 + P6-4** — authz middleware + audit for those builtins.  

Adjust ordering if **bare metal** becomes the primary track (move **P4*** earlier, defer **P3-3** TAP).

---

*Maintainers: when a roadmap item becomes committed work, record the **shipped** behavior under `version/entries/*.ver` per repository versioning policy; keep this document’s phases aligned with actual merges.*

# P0–P2 PR coverage (4.1.0 wiring branch)

This note separates **contract completion** (normative headers under `contracts/`) from **module integration** (hosted enforcement and tests). See `docs/ROADMAP.md` for the full P0–P9 snapshot.

## Wired in this PR (integration ✅ for hosted lab scope)

| Area | Evidence |
|------|----------|
| **P1-4 / P1-7** | `fl_stack`, PMM `fl_result_t` APIs, `fl_timekeeping`, `test_p0_p2_wiring`; GAS `fl_stack_asm.s` for stack count and elevation slot scan (x86_64 + AArch64 GAS builds) |
| **P2-2** | SQLite `user_db`, PBKDF2 password hashes, `users.lab.json` seed path |
| **P2-3** | `fl_authz_subsystem_check`, shell `fl_shell_authz_builtin`, guest deny on ≥3 privileged ops + identity admin cmds, shell↔subsystem parity test |
| **P2-4** | Elevation tokens, password-gated `sudo`/`su`, `sudo -k`, `logout`, `fl_audit_elevation_event` (who/when/reason when `FL_AUDIT=1`) |
| **P0 FS jail** | `fs_jail_check_access`, `.flmeta/properties.json`, elevation-aware facade |

## Module integration snapshot (**`docs/ROADMAP.md`**)

All **P0–P2** rows are **✅** or **~✅** in **Module integration** (**4.1.0** hosted/lab). Use **~✅** when wiring **looks** complete on **H** but **B**-path proof, arch timers, kernel-thread exec context, or in-source **`TODO(Codex)`** remain.

| IDs | Integration | Rationale |
|-----|-------------|-----------|
| **P0-1 … P0-3** | **✅** | Boundaries, **`fl_result_t`**, CI |
| **P0-4 … P0-8** | **~✅** | Arch paths built; bare-metal proof TODOs (**`arm_gic.c`**, **`idt_dispatch.c`**, **`fs_jail.c`**) |
| **P1-1** | **~✅** | Hosted **`malloc`** stack/heap context + tests; kernel-thread / **B** exec context TODO (**`exec_context.c`**) |
| **P1-2, P1-3** | **~✅** | **`mem_domain`** / spinlocks wired on **H**; flat/paged story and lock-order graph not closed on **B** (**`pmm.c`**) |
| **P1-4** | **~✅** | PMM + **`fl_stack`** wired; **`pmm.c`** bare-metal / NASM ASM TODOs |
| **P1-5, P1-6** | **~✅** | Arenas and driver tables on **H**; **P1→P2** **B** validation tied to **`pmm.c`** / driver reentrancy gates |
| **P1-7** | **~✅** | POSIX **`clock_gettime`** on **H** + tests; arch Generic Timer / **P0-5** tick on **B** TODO (**`timekeeping.c`**) |
| **P2-1, P2-2** | **~✅** | Session/principal + **`user_db`** on **H**; service-layer / non-hosted credential paths per phase gates |
| **P2-3** | **~✅** | Shell + **`fm_service`** authz + **`test_p0_p2_wiring`**; kernel netdev/mount/FileManager entry wiring still open (**TODO: P2-3** in **`docs/ROADMAP.md`**) |
| **P2-4** | **~✅** | Elevation/sudo/su shipped; polish TODOs (**logout** audit order, login-shell env, audit file tests) |

## Code TODO markers (Codex / CodeRabbit follow-ups)

These are **patch-scale** items: **`docs/ROADMAP.md`** marks the matching snapshot rows **~✅** in **Module integration** (legend explains the **`~`** prefix).

Tracked in-source where integration is still partial:

| Area | File | Topic |
|------|------|--------|
| P0-4..P0-8 | `fs_jail.c`, `arm_gic.c`, `idt_dispatch.c` | Bare-metal integration evidence |
| P1-1 | `exec_context.c` | Kernel-thread / **B** execution context (today hosted **`malloc`** only) |
| P1-7 | `timekeeping.c` | Arch timer source on **B** (Generic Timer / **P0-5**), not POSIX-only |
| P1 | `pmm.c` | DRIVERS_BAREMETAL lock-order graph + PMM/arena behavior |
| P1 | `fl_stack.c`, `Makefile` | NASM `fl_stack_asm` port |
| P2 | `elevation.c` | Optional ASM for grant slot search |
| P2 | `cmd_su.c`, `cmd_sudo.c` | Login-shell environment for `su -` / `sudo -i` |
| P2 | `cmd_logout.c` | Audit revoke after successful logout |
| P2 | `test_p0_p2_wiring.c` | Audit file assertions (use `test_audit_log`) |

## Quick validation

```bash
make test_p0_p2_wiring
make test_invariants
make
```

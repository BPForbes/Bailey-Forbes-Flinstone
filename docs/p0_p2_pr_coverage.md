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

## Contract ✅ / integration ⚠️ (not claimed complete here)

| IDs | Status | Notes |
|-----|--------|-------|
| **P0-4 … P0-8** | Contract ✅, integration ⚠️ | ARM GIC EOI, x86 IDT/GDT/tick, FDT, early UART — contracts exist; bare-metal proof is out of scope for this hosted PR |
| **P1-1 … P1-6** | Contract ✅, integration ⚠️ | PMM/arenas/lock-ordering on **B** and full **DRIVERS_BAREMETAL** graphs are follow-up (see ROADMAP P1→P2 gate) |

## Code TODO markers (Codex / CodeRabbit follow-ups)

These are **patch-scale** items: **`docs/ROADMAP.md`** marks the matching snapshot rows **~✅** in **Module integration** (legend explains the **`~`** prefix).

Tracked in-source where integration is still partial:

| Area | File | Topic |
|------|------|--------|
| P0-4..P0-8 | `fs_jail.c`, `arm_gic.c`, `idt_dispatch.c` | Bare-metal integration evidence |
| P1 | `pmm.c` | DRIVERS_BAREMETAL lock-order + PMM/arenas |
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

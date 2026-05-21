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

All **P0–P2** rows use **✅** or **~✅** in the roadmap **Module integration** column (**4.1.0** hosted/lab scope). **~✅** means wiring and tests are in place; remaining work is **patch-scale** (in-source **`TODO(Codex)`**, bare-metal proof on **B**, or items like **TODO: P2-3** kernel-path authz)—not a missing contract row.

| Band | Integration | Notes |
|------|-------------|-------|
| **P0-1 … P0-3** | **✅** | Subsystems, **`fl_result_t`**, CI |
| **P0-4 … P2-4** | **~✅** | Hosted wiring + **`test_p0_p2_wiring`**; see *Code TODO markers* below |

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

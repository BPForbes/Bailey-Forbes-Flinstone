# P0–P2 PR coverage (4.1.0 wiring branch)

This note separates **contract completion** (normative headers under `contracts/`) from **module integration** (hosted enforcement and tests). See `docs/ROADMAP.md` for the full P0–P9 snapshot.

## Wired in this PR (integration ✅ for hosted lab scope)

| Area | Evidence |
|------|----------|
| **P1-4 / P1-7** | `fl_stack`, PMM `fl_result_t` APIs, `fl_timekeeping`, `test_p0_p2_wiring`; GAS `fl_stack_asm.s` for stack count and elevation slot scan (x86_64 + AArch64 GAS builds) |
| **P2-2** | SQLite `user_db`, PBKDF2 password hashes, lab seed path |
| **P2-3** | `fl_authz_subsystem_check`, shell `fl_shell_authz_builtin`, guest deny on ≥3 privileged ops + identity admin cmds, shell↔subsystem parity test |
| **P2-4** | Elevation tokens, password-gated `sudo`/`su`, `sudo -k`/`-i`, `logout`, login-shell env (`session_login_env.c`), `fl_audit_elevation_event` + `test_audit_log` |
| **P0 FS jail** | `fs_jail_check_access`, `.flmeta/properties.json`, elevation-aware facade; `test_fs_jail` |

## Module integration snapshot (**`docs/ROADMAP.md`**)

| IDs | Integration | Rationale |
|-----|-------------|-----------|
| **P0-1 … P0-3** | **✅** | Boundaries, **`fl_result_t`**, CI |
| **P0-4, P0-5** | **~✅** | GIC/IDT paths built; bare-metal evidence TODOs in **`arm_gic.c`**, **`idt_dispatch.c`** |
| **P0-6 … P0-8** | **✅** | GDT/FDT/UART contract + hosted paths; no open **H** integration TODO |
| **P1-1** | **✅** | Hosted **`fl_exec_context`** + **`test_p0_p2_wiring`**; kernel-thread **B** tracked in phase gates |
| **P1-2, P1-3** | **~✅** | **`mem_domain`** / spinlocks on **H**; flat/paged + lock-order on **B** (**`pmm.c`**) |
| **P1-4** | **~✅** | PMM + **`fl_stack`** wired; **`pmm.c`** / NASM **`fl_stack_asm`** TODOs |
| **P1-5, P1-6** | **~✅** | Arenas and driver tables on **H**; **B** validation tied to **`pmm.c`** |
| **P1-7** | **✅** | POSIX **`clock_gettime`** on **H** + tests; arch timer on **B** is phase follow-up |
| **P2-1, P2-2** | **✅** | Session/principal + **`user_db`** on **H** |
| **P2-3** | **~✅** | Shell + **`fm_service`** authz + **`test_p0_p2_wiring`**; kernel netdev/mount/FileManager entry wiring still open (**TODO: P2-3** in **`docs/ROADMAP.md`**) |
| **P2-4** | **✅** | Elevation/sudo/su/logout/whoami, login-shell env, audit ordering, **`test_audit_log`** elevation lines |

## Contract-mount (GitHub #121–#128)

Implementation headers now include normative contract anchors (or new **P0** jail shard):

| Issue | Fix |
|-------|-----|
| **#121** | `exec_context.h` → `contract_p1_execution.h` |
| **#122** | `pmm.h`, `fl_stack.h` → `contract_p1_pmm.h` |
| **#123** | `timekeeping.h` → `contract_p1_timekeeping.h` |
| **#124** | `path_property.h` → `contract_p2_principal.h`, `contract_p2_authz.h` |
| **#125** | `fl/authz_subsystem.h` → `contract_p2_authz.h` |
| **#126** | `session.h` → `contract_identity.h` umbrella |
| **#127** | New `contracts/foundations/contract_p0_fs_jail.h`; `fs_jail.h` include; wired in `contract_foundations.h` |
| **#128** | `contract_p2_credential_store.h` SQLite path; **`FL_CONTRACT_P2_IDENTITY_REV` 3**; `FL_USERS_DB_DEFAULT_PATH` aliases contract macro |

## Remaining integration gaps (no in-source `TODO(P*/Codex)` on ✅ rows)

| Area | Tracking |
|------|----------|
| P0-4, P0-5 | **`arm_gic.c`**, **`idt_dispatch.c`** file comments; bare-metal proof per phase gates |
| P1-2–P1-6 | **`pmm.c`**, **`fl_stack.c`** (NASM port) |
| P2-3 | **`docs/ROADMAP.md`** netdev/mount/FileManager authz callouts |

## Quick validation

```bash
make test_p0_p2_wiring
make test_audit_log
make test_invariants
make
```

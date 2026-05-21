# P0–P2 PR coverage (4.1.0 wiring branch)

This note separates **contract completion** (normative headers under `contracts/`) from **module integration** (hosted enforcement and tests). See `docs/ROADMAP.md` for the full P0–P9 snapshot.

## Wired in this PR (integration ✅ for hosted lab scope)

| Area | Evidence |
|------|----------|
| **P1-4 / P1-7** | `fl_stack`, PMM `fl_result_t` APIs, `fl_timekeeping`, `test_p0_p2_wiring` |
| **P2-2** | SQLite `user_db`, PBKDF2 password hashes, `users.lab.json` seed path |
| **P2-3** | `fl_authz_subsystem_check`, shell `fl_shell_authz_builtin`, guest deny on ≥3 privileged ops + identity admin cmds, shell↔subsystem parity test |
| **P2-4** | Elevation tokens, password-gated `sudo`/`su`, `sudo -k`, `logout`, `fl_audit_elevation_event` (who/when/reason when `FL_AUDIT=1`) |
| **P0 FS jail** | `fs_jail_check_access`, `.flmeta/properties.json`, elevation-aware facade |

## Contract ✅ / integration ⚠️ (not claimed complete here)

| IDs | Status | Notes |
|-----|--------|-------|
| **P0-4 … P0-8** | Contract ✅, integration ⚠️ | ARM GIC EOI, x86 IDT/GDT/tick, FDT, early UART — contracts exist; bare-metal proof is out of scope for this hosted PR |
| **P1-1 … P1-6** | Contract ✅, integration ⚠️ | PMM/arenas/lock-ordering on **B** and full **DRIVERS_BAREMETAL** graphs are follow-up (see ROADMAP P1→P2 gate) |

## Quick validation

```bash
make test_p0_p2_wiring
make test_invariants
make
```

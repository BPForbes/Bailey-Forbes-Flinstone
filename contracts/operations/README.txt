P7 (*contracts/operations*) — roadmap **Phase 7** (**P7-1** service supervision, **P7-2**
packaging, **P7-3** remote admin path). See **docs/ROADMAP.md** Phase **7** table.

**Umbrella header:** *contract_operations.h* — includes **contract_extend.h** (P0 plus P1),
**FL_CONTRACT_P7_OPERATIONS_REV**, all shards below, and **FL_CONTRACT_P7_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P7_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
| *contract_p7_service_supervision.h* | P7-1 (service state enum, restart caps, supervised service table cap) |
| *contract_p7_packaging.h* | P7-2 (semver component max, prerelease tag cap, **SOURCE_DATE_EPOCH**, artifact path cap) |
| *contract_p7_remote_admin.h* | P7-3 (composes **contract_p6_audit_trail.h** for audit evt) |
| *contract_p7_shell_batch.h* | Shell batch argv (per-builtin caps + **FL_CONTRACT_P7_BATCH_MAX_TOKENS_TOTAL**) |

Each shard includes **contract_extend.h** so standalone use inherits **P0** vocabulary before **P7**.

**Build:** add **-Icontracts/operations** next to **-Icontracts/observability** in the root **Makefile**
**CFLAGS** and in **CMakeLists.txt** include directories for targets that compile contract-aware code.

**Layering:** this bundle does **not** include **contract_observability.h**. Include it explicitly when
remote-admin enablement must record **P6-4** audit events.

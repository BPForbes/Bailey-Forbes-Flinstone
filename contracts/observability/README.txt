P6 (*contracts/observability*) — roadmap **Phase 6** (**P6-1** structured log API,
**P6-2** ring buffer sink, **P6-3** persistent log (hosted), **P6-4** audit trail,
**P6-5** tracing hooks). See **docs/ROADMAP.md** Phase **6** table.

**Umbrella header:** *contract_observability.h* — includes **contract_extend.h** (P0 plus P1),
**FL_CONTRACT_P6_OBSERVABILITY_REV**, all shards below, and **FL_CONTRACT_P6_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P6_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
| *contract_p6_structured_log.h* | P6-1 (min level, correlation id, filter hooks; composes **contract_log.h**) |
| *contract_p6_ring_buffer.h* | P6-2 |
| *contract_p6_persistent_log.h* | P6-3 (rotation file cap, disk budget) |
| *contract_p6_audit_trail.h* | P6-4 (event kind enum) |
| *contract_p6_tracing.h* | P6-5 (**VFS** tracepoint; ties to correlation id) |

Each shard includes **contract_extend.h** so standalone use inherits **P0** vocabulary before **P6**.

**Build:** add **-Icontracts/observability** next to **-Icontracts/storage** in the root **Makefile**
**CFLAGS** and in **CMakeLists.txt** include directories for targets that compile contract-aware code.

**Layering:** this bundle does **not** include **contract_storage.h** or **contract_identity.h**.
Include them explicitly when a translation unit spans mount/authz and observability surfaces.

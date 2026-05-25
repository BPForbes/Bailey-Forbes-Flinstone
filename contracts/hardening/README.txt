P9 (*contracts/hardening*) — roadmap **Phase 9** (**P9-1** fuzzing, **P9-2** static analysis,
**P9-3** SMP bring-up on **B**, **P9-4** RCU grace jobs). See **docs/ROADMAP.md** Phase **9** table.

**Umbrella header:** *contract_hardening.h* — includes **contract_extend.h** (P0),
**FL_CONTRACT_P9_HARDENING_REV**, and shards below with **`FL_CONTRACT_P9_*_CONTRACT_DEFINED`**
markers.

**Shards (normative comments + contract-defined markers):**

| File | Roadmap |
|------|---------|
| *contract_p9_fuzz.h* | **P9-1** — fuzz input size cap, corpus path cap, crash-is-bug default |
| *contract_p9_static_analysis.h* | **P9-2** — SA severity enum, zero-new-critical gate, ruleset token |
| *contract_p9_smp.h* | **P9-3** — SMP lock-order inherits **P1-3**, AArch64 **PSCI CPU_ON** (**P4-7**) |
| *contract_p9_rcu.h* | **P9-4** — RCU grace-period work on **P1-8** |

**Build:** **-Icontracts/hardening** is already in the root **Makefile** **CFLAGS** and
**CMakeLists.txt** **include_directories**.

**Layering:** extends **`contract_extend.h`** only. **Fuzz engines** and **Coverity** APIs stay
out-of-tree; this bundle names **interchange caps** for harnesses and CI gates. **SMP execution**
still depends on **`contracts/runtime/`** and **`contracts/drivers/`** (**PSCI**); include those
umbrellas in TUs that implement bring-up, not in every consumer of **P9-2** alone.

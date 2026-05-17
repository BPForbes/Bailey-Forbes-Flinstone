P1 (*contracts/runtime*) — roadmap **P1-1** … **P1-7** (execution context, address
space, preemption, PMM, domain arenas, driver reentrancy, timekeeping).

**Umbrella header:** *contract_runtime.h* — includes **contract_extend.h** (P0
base), **FL_CONTRACT_P1_RUNTIME_REV**, all shards below, and
**FL_CONTRACT_P1_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P1_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
| *contract_p1_execution.h* | P1-1 |
| *contract_p1_address_space.h* | P1-2 |
| *contract_p1_preemption.h* | P1-3 |
| *contract_p1_pmm.h* | P1-4 |
| *contract_p1_arenas.h* | P1-5 |
| *contract_p1_driver_reentrancy.h* | P1-6 |
| *contract_p1_timekeeping.h* | P1-7 |

Each shard starts with **#include "contract_extend.h"** (see *../foundations/*).

**Build:** add **-Icontracts/runtime** next to **-Icontracts/foundations** in the
root **Makefile** **CFLAGS** and in **CMakeLists.txt** targets that already carry
the foundations include path.

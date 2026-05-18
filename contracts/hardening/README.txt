P9 (*contracts/hardening*) — roadmap **Phase 9** (**P9-1** fuzzing, **P9-2** static analysis,
**P9-3** SMP bring-up on **B**). See **docs/ROADMAP.md** Phase **9** table.

**Status:** directory reserved for the future **P9** normative C bundle (umbrella + **`contract_p9_*.h`**
shards with **`FL_CONTRACT_P9_*_CONTRACT_DEFINED`** markers), following the pattern used under
**`contracts/foundations/`** through **`contracts/operations/`**.

**Process note:** fuzz and static-analysis **gates** may stay **process-only** until promoted into
contract rows; see the **P0–P9 snapshot** legend in **docs/ROADMAP.md**.

**Build:** when headers land, add **-Icontracts/hardening** next to **-Icontracts/operations** in the
root **Makefile** **CFLAGS** and in **CMakeLists.txt** **include_directories** (already wired for
early adoption).

**Layering:** SMP contracts compose **P1-3** / **P1-6** and **P4-7** (**PSCI**); this bundle records
**scale** and analysis vocabulary without replacing **`contracts/runtime/`** or **`contracts/drivers/`**.

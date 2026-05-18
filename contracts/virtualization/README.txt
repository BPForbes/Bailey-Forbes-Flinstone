P8 (*contracts/virtualization*) — roadmap **Phase 8** (**P8-1** device timing fidelity, **P8-2**
guest virtio). See **docs/ROADMAP.md** Phase **8** table.

**Status:** directory reserved for the future **P8** normative C bundle (umbrella + **`contract_p8_*.h`**
shards with **`FL_CONTRACT_P8_*_CONTRACT_DEFINED`** markers), following the pattern used under
**`contracts/foundations/`** through **`contracts/operations/`**.

**Build:** when headers land, add **-Icontracts/virtualization** next to **-Icontracts/operations** in
the root **Makefile** **CFLAGS** and in **CMakeLists.txt** **include_directories** (already wired for
early adoption).

**Layering:** this bundle will extend **`contract_extend.h`**; it must **not** duplicate **P4**
virtio transport setup (**`contracts/drivers/`**) or **P3** IP/datagram paths (**`contracts/networking/`**).

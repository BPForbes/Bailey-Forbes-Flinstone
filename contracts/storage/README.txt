P5 (*contracts/storage*) — roadmap **Phase 5** (**P5-1** VFS layer, **P5-2** pluggable FS,
**P5-3** page cache). See **docs/ROADMAP.md** Phase **5** table.

**Umbrella header:** *contract_storage.h* — includes **contract_extend.h** (P0 plus P1),
**FL_CONTRACT_P5_STORAGE_REV**, all shards below, and **FL_CONTRACT_P5_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P5_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
| *contract_p5_vfs.h* | P5-1 (open-fd cap, dirent name, mode bits, mount flags, durability class) |
| *contract_p5_pluggable_fs.h* | P5-2 |
| *contract_p5_page_cache.h* | P5-3 (max entries, dirty ratio, writeback batch) |

Each shard includes **contract_extend.h** so standalone use inherits **P0** vocabulary before **P5**.

**Build:** add **-Icontracts/storage** next to **-Icontracts/drivers** in the root **Makefile**
**CFLAGS** and in **CMakeLists.txt** include directories for targets that compile contract-aware code.

**Layering:** this bundle does **not** include **contract_networking.h** or **contract_drivers.h**.
Include them explicitly when a translation unit spans virtio/block and the VFS.

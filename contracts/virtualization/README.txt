P8 (*contracts/virtualization*) — roadmap **Phase 8** (**P8-1** device timing fidelity, **P8-2**
guest virtio, **P8-3** QEMU lab machine profiles). See **docs/ROADMAP.md** Phase **8** table.

**Umbrella header:** *contract_virtualization.h* — includes **contract_extend.h** (P0),
**FL_CONTRACT_P8_VIRTUALIZATION_REV**, and shards below with **`FL_CONTRACT_P8_*_CONTRACT_DEFINED`**
markers.

**Shards (normative comments + contract-defined markers):**

| File | Roadmap |
|------|---------|
| *contract_p8_timing.h* | **P8-1** — replay-forward event order, documented lab timing jitter |
| *contract_p8_virtio_guest.h* | **P8-2** — guest RAM ownership vs **P4-4** ring programming |
| *contract_p8_qemu_lab.h* | **P8-3** — QEMU `-M` profile strings, fixture path caps, accel class enum |

**Build:** **-Icontracts/virtualization** is already in the root **Makefile** **CFLAGS** and
**CMakeLists.txt** **include_directories**.

**Layering:** this bundle extends **`contract_extend.h`** only. **Virtqueue mechanics** stay in
**`contracts/drivers/`** (**`contract_p4_virtio.h`**); **IP/datagram paths** stay **P3**
(**`contracts/networking/`**). Include **`contract_drivers.h`** only where rings are programmed.

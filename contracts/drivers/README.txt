P4 (*contracts/drivers*) — roadmap **P4-1** … **P4-8** (driver model v2, IRQ lifecycle,
PCIe lab config, virtio net/block, USB xHCI subset, FDT discovery lab, PSCI client).

**Umbrella header:** *contract_drivers.h* — includes **contract_extend.h** (P0–P1),
**FL_CONTRACT_P4_DRIVERS_REV**, all shards below, and **FL_CONTRACT_P4_VOCABULARY_LOCK**.

**Shards (normative comments + **FL_CONTRACT_P4_*_CONTRACT_DEFINED** markers):**

| File | Roadmap |
|------|---------|
| *contract_p4_driver_model.h* | P4-1 |
| *contract_p4_irq_lifecycle.h* | P4-2 |
| *contract_p4_pcie_lab.h* | P4-3 |
| *contract_p4_virtio.h* | P4-4 |
| *contract_p4_usb.h* | P4-5 (xHCI subset + ASM MMIO glue) |
| *contract_p4_fdt_discovery.h* | P4-6 |
| *contract_p4_psci.h* | P4-7 |
| *contract_p4_kworker.h* | P4-8 |

**Networking:** **P3-1** netdev framing and **P2-3** netdev authz ops remain under
**contracts/networking/**; **P4** covers **hardware bring-up**, **IRQ**, **PCIe**, **virtio**,
**FDT** enumeration policy, and **PSCI** — not IP/datagram paths.

**Build:** add **-Icontracts/drivers** next to **-Icontracts/networking** in the root
**Makefile** **CFLAGS** and in **CMakeLists.txt** **include_directories** for targets that
compile driver-facing contracts.

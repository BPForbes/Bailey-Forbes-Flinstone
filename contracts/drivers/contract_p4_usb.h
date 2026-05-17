/**
 * **P4-5 — USB stack (xHCI subset, normative contract)** (module contract).
 *
 * **Scope:** this row targets **USB 3.x xHCI** host-controller bring-up on **lab** targets
 * (**QEMU xHCI** class, one **PCIe** function) before claiming a general-purpose **USB**
 * stack. **Compliance suites** and **hub trees** remain **out-of-tree** product work.
 *
 * **Distribution — MMIO:** **Capability**, **operational**, **runtime**, and **doorbell**
 * register groups live in a single **PCI BAR** window. **Doorbell** writes are **ordered
 * after** the driver makes **TRB** chains and **segment** pointers visible to the
 * controller; use **`fl_usb_xhci_mmio_rw_fence`** (see **`fl/driver/usb_xhci_mmio_glue.h`**)
 * at ring publish boundaries on **B** and **K** builds, not only a C **volatile** load.
 *
 * **Distribution — rings:** **Command**, **Event**, and **Transfer** rings are **owned**
 * by software while **TRBs** are **non-executed**; the controller **consumes** a **TRB** only
 * after the **Cycle** bit and **pointer** updates match **Intel xHCI** §4.9 ordering rules.
 * **DMA buffers** for **TRB** payloads follow **P1** memory-domain rules where applicable.
 *
 * **IRQ:** **MSI** or **MSI-X** (preferred) vs **INTx** pin delivery is a **registration**
 * surface under **P4-2**; **Event ring** processing runs in the **bottom half** contract.
 *
 * **ASM layering:** device-register **read32**, **write32**, and **rw_fence** for this row
 * live in **usb_xhci_mmio_asm** translation units under **kernel/arch** (one file per host
 * ISA) and are surfaced through **`fl_usb_xhci_mmio_*_volatile`** and
 * **usb_xhci_mmio_glue.c** under **kernel/drivers** so **hot** paths stay in arch assembly
 * while **H** hosted builds may still compile the glue for tests and early bring-up.
 */
#ifndef FL_CONTRACT_P4_USB_H
#define FL_CONTRACT_P4_USB_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_5_USB_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_USB_H */

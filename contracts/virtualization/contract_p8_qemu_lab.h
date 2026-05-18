/* **P8-3 — QEMU / machine emulation (lab)** (module contract, normative).
 *
 * **Distribution:** **Lab bring-up** documents which **QEMU machine types** (`-M`), **CPU
 * models**, and **acceleration** choices (**TCG** vs **KVM**) are supported for **golden
 * vectors** and **replay** fixtures. **Firmware and DTB blobs** are named inputs with **path /
 * size caps**; producers record **hashes** where reproducibility is required.
 *
 * **Outcomes:** missing fixtures or unknown profiles return **`fl_result_t`** (**P0-2**), not
 * silent fallback to a different machine type.
 *
 * **Scope:** this is **hosted lab** vocabulary only unless a phase table row promotes **B**
 * parity; KVM on metal reuses the same **profile naming** for CI ↔ developer convergence.
 */
#ifndef FL_CONTRACT_P8_QEMU_LAB_H
#define FL_CONTRACT_P8_QEMU_LAB_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P8_3_QEMU_LAB_CONTRACT_DEFINED 1

/** Inclusive max chars for QEMU `-M` machine profile token in lab metadata tables. */
#define FL_CONTRACT_P8_QEMU_MACHINE_NAME_MAX_CHARS 63u

/** Inclusive max chars for documented fixture path strings (replay disk, DTB, flash images). */
#define FL_CONTRACT_P8_QEMU_FIXTURE_PATH_MAX_CHARS 4096u

typedef enum {
    FL_CONTRACT_P8_QEMU_ACCEL_UNKNOWN = 0,
    FL_CONTRACT_P8_QEMU_ACCEL_TCG = 1,
    FL_CONTRACT_P8_QEMU_ACCEL_KVM = 2,
    FL_CONTRACT_P8_QEMU_ACCEL_HVF_DEFERRED = 3
} fl_contract_p8_qemu_accel_class_t;

_Static_assert(FL_CONTRACT_P8_QEMU_MACHINE_NAME_MAX_CHARS >= 15u,
               "machine token cap must fit common names like pc-q35-2.10");

#endif /* FL_CONTRACT_P8_QEMU_LAB_H */

/**
 * **P4-7 — PSCI client (AArch64)** (module contract, normative).
 *
 * **Distribution:** **SMC** calls into **ARM PSCI** (**DEN0022**) return **concrete**
 * **status** words; **`CPU_ON`**, **`CPU_OFF`**, and **`CPU_SUSPEND`** arguments are **owned**
 * by the caller until the firmware acknowledges. **DT** **`psci`** node (`arm,psci-1.0`
 * bindings) is the **discovery** surface; **TF-A** on **QEMU** is informative only.
 *
 * **Outcomes:** SMC failures map to **`fl_result_t`** (**P0-2**); negative PSCI status
 * words are not silently treated as success.
 */
#ifndef FL_CONTRACT_P4_PSCI_H
#define FL_CONTRACT_P4_PSCI_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_7_PSCI_CONTRACT_DEFINED 1

/** PSCI function identifiers (informative; platform firmware implements). */
#define FL_CONTRACT_P4_PSCI_CPU_ON 0xC4000003u
#define FL_CONTRACT_P4_PSCI_CPU_OFF 0x84000002u
#define FL_CONTRACT_P4_PSCI_CPU_SUSPEND 0xC4000001u

#endif /* FL_CONTRACT_P4_PSCI_H */

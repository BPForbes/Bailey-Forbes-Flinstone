/**
 * **P4-7 — PSCI client (AArch64)** (module contract, normative).
 *
 * **Distribution:** **SMC** calls into **ARM PSCI** (**DEN0022**) return **concrete**
 * **status** words; **`CPU_ON`**, **`CPU_OFF`**, and **`CPU_SUSPEND`** arguments are **owned**
 * by the caller until the firmware acknowledges. **DT** **`psci`** node (`arm,psci-1.0`
 * bindings) is the **discovery** surface; **TF-A** on **QEMU** is informative only.
 */
#ifndef FL_CONTRACT_P4_PSCI_H
#define FL_CONTRACT_P4_PSCI_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_7_PSCI_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_PSCI_H */

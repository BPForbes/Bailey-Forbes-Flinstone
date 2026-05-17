/**
 * **P4-6 — FDT-driven machine discovery (lab)** (module contract, normative).
 *
 * **Distribution:** **DTB** bytes from **P0-7** are walked read-only to enumerate **memory**,
 * **`cpus`**, **`psci`**, and **`compatible`** strings for driver match tables. Parsing may
 * split between **loader** and **kernel-shaped** layers; the **contract** names which node
 * types are **authoritative** for each consumer and where **aliases** are resolved.
 */
#ifndef FL_CONTRACT_P4_FDT_DISCOVERY_H
#define FL_CONTRACT_P4_FDT_DISCOVERY_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_6_FDT_DISCOVERY_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_FDT_DISCOVERY_H */

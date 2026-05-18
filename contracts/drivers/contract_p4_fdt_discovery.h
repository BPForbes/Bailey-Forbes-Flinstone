/**
 * **P4-6 — FDT-driven machine discovery (lab)** (module contract, normative).
 *
 * **Distribution:** **DTB** bytes from **P0-7** are walked read-only to enumerate **memory**,
 * **`cpus`**, **`psci`**, and **`compatible`** strings for driver match tables. Parsing may
 * split between **loader** and **kernel-shaped** layers; the **contract** names which node
 * types are **authoritative** for each consumer and where **aliases** are resolved.
 *
 * **Outcomes:** malformed DTB returns **FL_RESULT_INVAL**; missing required nodes return
 * **FL_RESULT_NOENT** (**P0-2**, aligned with **P0-7** walker contract).
 */
#ifndef FL_CONTRACT_P4_FDT_DISCOVERY_H
#define FL_CONTRACT_P4_FDT_DISCOVERY_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_6_FDT_DISCOVERY_CONTRACT_DEFINED 1

/** Node kinds **P4** lab enumeration must surface (read-only DTB walk). */
#define FL_CONTRACT_P4_FDT_NODE_MEMORY 0
#define FL_CONTRACT_P4_FDT_NODE_CPUS 1
#define FL_CONTRACT_P4_FDT_NODE_PSCI 2
#define FL_CONTRACT_P4_FDT_NODE_COMPATIBLE 3

#endif /* FL_CONTRACT_P4_FDT_DISCOVERY_H */

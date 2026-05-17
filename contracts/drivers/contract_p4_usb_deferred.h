/**
 * **P4-5 — USB stack `[DEFERRED]`** (module contract, normative).
 *
 * **Explicit deferral** — matches the **Phase 4** table: full **USB** is **Phase 4+**.
 * When started, scope to **xHCI** subset only; **compliance** testing remains out-of-tree.
 * This shard exists so the **P4** bundle does not silently omit the row.
 */
#ifndef FL_CONTRACT_P4_USB_DEFERRED_H
#define FL_CONTRACT_P4_USB_DEFERRED_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_5_USB_DEFERRED_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_USB_DEFERRED_H */

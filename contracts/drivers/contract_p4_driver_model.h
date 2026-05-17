/**
 * **P4-1 — Driver model v2** (module contract, normative).
 *
 * **Distribution:** driver **registration**, **probe** / **attach** / **remove** lifecycle,
 * optional **power** transitions, and **DMA capability** masks are modeled as explicit
 * outcomes on **`fl_result_t`** (see **P0-2**) with **decline** paths analogous to
 * **FL_RESULT_PROBE_SKIP** where the implementation already uses them. Static driver
 * tables guarded by **`s_model_lock`** (**Appendix D**) are an **enforcement** concern;
 * this header records the **obligation** that published tables are not mutated without the
 * documented lock ordering.
 */
#ifndef FL_CONTRACT_P4_DRIVER_MODEL_H
#define FL_CONTRACT_P4_DRIVER_MODEL_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_1_DRIVER_MODEL_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_DRIVER_MODEL_H */

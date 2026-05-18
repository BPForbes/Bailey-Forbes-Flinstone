/*
 * P9 hardening / compliance / scale contract bundle (contracts/hardening).
 *
 * Include this header (or individual contract_p9_*.h after contract_extend.h) when implementing
 * or reviewing **P9-1** fuzz harnesses, **P9-2** static-analysis gates, and **P9-3** SMP bring-up
 * vocabulary. Build with -Icontracts/hardening alongside other contract bundle -I flags.
 *
 * Layering: extends contract_extend.h (P0 vocabulary). It does **not** import **P8**
 * virtualization headers or **P4** drivers wholesale—include those bundles only where a TU
 * actually crosses those boundaries.
 *
 * Bump FL_CONTRACT_P9_HARDENING_REV when shard set or mandatory P9 vocabulary changes.
 */
#ifndef FL_CONTRACT_HARDENING_H
#define FL_CONTRACT_HARDENING_H

#include "contract_extend.h"

#define FL_CONTRACT_P9_HARDENING_REV 1

#include "contract_p9_fuzz.h"
#include "contract_p9_static_analysis.h"
#include "contract_p9_smp.h"

#define FL_CONTRACT_P9_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P9_VOCABULARY_LOCK == 1,
               "P9 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_HARDENING_H */

/*
 * P6 observability / system loggers contract bundle (contracts/observability).
 *
 * Include this header (or individual contract_p6_*.h after contract_extend.h) when
 * implementing or reviewing P6-1 through P6-5 work.
 * Build with -Icontracts/observability alongside -Icontracts/storage, -Icontracts/drivers,
 * -Icontracts/networking, -Icontracts/identity, -Icontracts/runtime, and
 * -Icontracts/foundations.
 *
 * Layering: extends contract_extend.h (P0 plus P1 vocabulary). **P6-1** composes
 * **contract_log.h** from foundations; it does not import **contract_storage.h** or
 * **contract_identity.h** — include those explicitly when a translation unit spans VFS
 * or authz audit wiring.
 *
 * Bump FL_CONTRACT_P6_OBSERVABILITY_REV when shard set or mandatory P6 vocabulary changes.
 */
#ifndef FL_CONTRACT_OBSERVABILITY_H
#define FL_CONTRACT_OBSERVABILITY_H

#include "contract_extend.h"

#define FL_CONTRACT_P6_OBSERVABILITY_REV 2

#include "contract_p6_structured_log.h"
#include "contract_p6_ring_buffer.h"
#include "contract_p6_persistent_log.h"
#include "contract_p6_audit_trail.h"
#include "contract_p6_tracing.h"

#define FL_CONTRACT_P6_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P6_VOCABULARY_LOCK == 1,
               "P6 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_OBSERVABILITY_H */

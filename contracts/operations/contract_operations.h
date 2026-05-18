/*
 * P7 shell UX / ops / packaging contract bundle (contracts/operations).
 *
 * Include this header (or individual contract_p7_*.h after contract_extend.h) when
 * implementing or reviewing P7-1 through P7-3 work (plus shell batch argv caps).
 * Build with -Icontracts/operations alongside -Icontracts/observability,
 * -Icontracts/storage, -Icontracts/drivers, -Icontracts/networking,
 * -Icontracts/identity, -Icontracts/runtime, and -Icontracts/foundations.
 *
 * Layering: extends contract_extend.h (P0 plus P1 vocabulary). It does not import
 * contract_observability.h — include it explicitly when remote-admin enablement must
 * audit via **P6-4**.
 *
 * Bump FL_CONTRACT_P7_OPERATIONS_REV when shard set or mandatory P7 vocabulary changes.
 */
#ifndef FL_CONTRACT_OPERATIONS_H
#define FL_CONTRACT_OPERATIONS_H

#include "contract_extend.h"

#define FL_CONTRACT_P7_OPERATIONS_REV 2

#include "contract_p7_service_supervision.h"
#include "contract_p7_packaging.h"
#include "contract_p7_remote_admin.h"
#include "contract_p7_shell_batch.h"

#define FL_CONTRACT_P7_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P7_VOCABULARY_LOCK == 1,
               "P7 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_OPERATIONS_H */

/*
 * P1 core runtime contract bundle (directory contracts/runtime, see README).
 *
 * Include this header (or individual contract_p1_*.h shards after
 * contract_extend.h) when implementing or reviewing P1-1 through P1-7 work.
 * Build with -Icontracts/runtime alongside -Icontracts/foundations.
 *
 * Bump FL_CONTRACT_P1_RUNTIME_REV when shard set or mandatory P1 vocabulary changes.
 */
#ifndef FL_CONTRACT_RUNTIME_H
#define FL_CONTRACT_RUNTIME_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_RUNTIME_REV 1

#include "contract_p1_execution.h"
#include "contract_p1_address_space.h"
#include "contract_p1_preemption.h"
#include "contract_p1_pmm.h"
#include "contract_p1_arenas.h"
#include "contract_p1_driver_reentrancy.h"
#include "contract_p1_timekeeping.h"

#define FL_CONTRACT_P1_VOCABULARY_LOCK 1

#endif /* FL_CONTRACT_RUNTIME_H */

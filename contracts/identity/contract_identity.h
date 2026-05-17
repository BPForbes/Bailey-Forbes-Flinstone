/*
 * P2 identity and access-control contract bundle (contracts/identity).
 *
 * Include this header (or individual contract_p2_*.h after contract_runtime.h)
 * when implementing or reviewing P2-1 through P2-4 work.
 * Build with -Icontracts/identity alongside -Icontracts/runtime and -Icontracts/foundations.
 *
 * Inheritance: contract_runtime.h brings P1 runtime shards on top of P0
 * (contract_extend.h pulls contract_foundations.h). This bundle adds P2 only.
 *
 * Bump FL_CONTRACT_P2_IDENTITY_REV when shard set or mandatory P2 vocabulary changes.
 */
#ifndef FL_CONTRACT_IDENTITY_H
#define FL_CONTRACT_IDENTITY_H

#include "contract_runtime.h"

#define FL_CONTRACT_P2_IDENTITY_REV 1

#include "contract_p2_principal.h"
#include "contract_p2_credential_store.h"
#include "contract_p2_authz.h"
#include "contract_p2_elevation.h"

#define FL_CONTRACT_P2_VOCABULARY_LOCK 1

#endif /* FL_CONTRACT_IDENTITY_H */

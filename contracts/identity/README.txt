P2 (contracts/identity) — roadmap P2-1 through P2-4 (principal model, credential store
hosted, authorization middleware, sudo-like elevation hosted).

Umbrella header: contract_identity.h — includes contract_runtime.h (P1 bundle on P0),
FL_CONTRACT_P2_IDENTITY_REV, all shards below, and FL_CONTRACT_P2_VOCABULARY_LOCK.

Shards (normative comments plus FL_CONTRACT_P2_*_CONTRACT_DEFINED markers):

| File | Roadmap |
|------|---------|
| contract_p2_principal_names.h | P2-1 literals (FL_PRINCIPAL / guest); no other includes |
| contract_p2_principal.h | P2-1 |
| contract_p2_credential_store.h | P2-2 |
| contract_p2_authz.h | P2-3 |
| contract_p2_elevation.h | P2-4 |

Each shard includes contract_runtime.h so standalone use inherits P0 and P1 before P2.

Build: add -Icontracts/identity next to -Icontracts/runtime and -Icontracts/foundations
in the root Makefile CFLAGS and in CMake targets that already carry those paths.

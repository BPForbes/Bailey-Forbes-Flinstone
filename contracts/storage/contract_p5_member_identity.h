/**
 * **P5-7 — Server member identity** (module contract, normative).
 *
 * **Distribution:** display name defaults to the logged-in shell principal. When two
 * or more connected members share the same principal string, the server assigns a
 * stable **member_id** disambiguator (hardware token hash and/or join-order slot).
 */
#ifndef FL_CONTRACT_P5_MEMBER_IDENTITY_H
#define FL_CONTRACT_P5_MEMBER_IDENTITY_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P5_7_MEMBER_IDENTITY_CONTRACT_DEFINED 1

#ifndef FL_SERVER_MEMBER_PRINCIPAL_MAX
#define FL_SERVER_MEMBER_PRINCIPAL_MAX 64u
#endif

#ifndef FL_SERVER_MEMBER_ID_MAX
#define FL_SERVER_MEMBER_ID_MAX 65535u
#endif

typedef struct {
    char principal[FL_SERVER_MEMBER_PRINCIPAL_MAX];
    uint32_t member_id;
    /** **1** when **member_id** was allocated because of a principal collision. */
    uint8_t disambiguated;
} fl_server_member_t;

_Static_assert(FL_SERVER_MEMBER_PRINCIPAL_MAX >= 16u, "principal label cap too small");

#endif /* FL_CONTRACT_P5_MEMBER_IDENTITY_H */

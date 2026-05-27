/**
 * P5-7 — Server member identity (module contract, normative).
 *
 * Distribution: display name defaults to the logged-in shell principal. The host
 * assigns a stable member_id (1..N) per connected TCP session at HELLO. Duplicate
 * principals on one session always get distinct ids. See docs/SERVER.md for
 * server connected, optional -id on send, and local vs host-global nicknames.
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

#ifndef FL_SERVER_NICK_MAX
#define FL_SERVER_NICK_MAX 64u
#endif

/** Session list slot shown by server connected ([1], [2], ...). */
typedef uint32_t fl_server_member_id_t;

typedef enum {
    FL_SERVER_NICK_SCOPE_NONE = 0,
    /** Client-only alias; only the setter sees {nick} in server connected. */
    FL_SERVER_NICK_SCOPE_LOCAL = 1,
    /** Host-assigned alias; every member sees {nick} and may target -user nick. */
    FL_SERVER_NICK_SCOPE_HOST_GLOBAL = 2
} fl_server_nick_scope_t;

typedef struct {
    char principal[FL_SERVER_MEMBER_PRINCIPAL_MAX];
    fl_server_member_id_t member_id;
    /** 1 when member_id was allocated because of a principal collision. */
    uint8_t disambiguated;
    /** 1 when this member is the session host. */
    uint8_t is_host;
    /** Host-global nickname (empty if none). */
    char host_nick[FL_SERVER_NICK_MAX];
} fl_server_member_t;

/**
 * Per-viewer local nickname overlay (not replicated on the wire except in UI).
 * Stored client-side keyed by (viewer_member_id, target_member_id).
 */
typedef struct {
    fl_server_member_id_t target_member_id;
    char local_nick[FL_SERVER_NICK_MAX];
} fl_server_local_nick_t;

_Static_assert(FL_SERVER_MEMBER_PRINCIPAL_MAX >= 16u, "principal label cap too small");
_Static_assert(FL_SERVER_NICK_MAX >= 8u, "nickname cap too small");

#endif /* FL_CONTRACT_P5_MEMBER_IDENTITY_H */

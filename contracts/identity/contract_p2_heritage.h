/**
 * **P2-1 / P2-3 — Account heritage, homes and bridges** (module contract, normative).
 *
 * Every account carries a **key id**: a dotted label naming the account and the
 * line of accounts that created it, in the same shape as a KeyQuorum label
 * (**`Root.Flinstone.Bob`**). **`Root`** is the master account at the top of the
 * tree and the only key id with one segment; every other key id is its
 * creator's key id, **`FL_HERITAGE_SEP`**, and the new account's name.
 *
 * Obligations:
 *   - A key id is fixed when its account is created and is never rewritten; the
 *     creator is the logged-in account, not a **sudo** target.
 *   - Authority flows down the tree only: an account may act for itself and its
 *     descendants (**FL_HERITAGE_REL_SELF**, **FL_HERITAGE_REL_DESCENDANT** from the
 *     actor's point of view), never for an ancestor, sibling or another branch.
 *   - Accounts on different branches reach each other only through an
 *     **active** bridge. A pending bridge grants nothing; a bridge joins exactly
 *     its two accounts, and neither their ancestors nor their descendants
 *     inherit it.
 *   - On a jailed host (**P0** fs jail active) each account's home is
 *     **`<jail>/FL_HERITAGE_HOME_DIR/<segments after Root>`**, so homes nest the
 *     way key ids do and the owner of a path is the deepest account whose home
 *     contains it.
 *
 * Wire and storage values (**fl_heritage_relation_t**, **fl_bridge_state_t**)
 * are append-only; bump **FL_CONTRACT_P2_IDENTITY_REV** when this vocabulary
 * changes.
 */
#ifndef FL_CONTRACT_P2_HERITAGE_H
#define FL_CONTRACT_P2_HERITAGE_H

#include "contract_runtime.h"

#define FL_CONTRACT_P2_HERITAGE_CONTRACT_DEFINED 1

/** The master account's key id, and the first segment of every other key id. */
#define FL_HERITAGE_ROOT_KEY "Root"
#define FL_HERITAGE_SEP '.'
/** Longest account name (one segment), excluding the terminator. */
#define FL_HERITAGE_SEGMENT_MAX_CHARS 31u
/** Deepest heritage line, **Root** included. */
#define FL_HERITAGE_MAX_DEPTH 8u
/** Key id buffer size: every segment at its longest, separators and terminator. */
#define FL_HERITAGE_KEY_ID_MAX \
    (FL_HERITAGE_MAX_DEPTH * (FL_HERITAGE_SEGMENT_MAX_CHARS + 1u))
/** Directory under the jail root that holds every account home. */
#define FL_HERITAGE_HOME_DIR "home"

/** How account **b** stands relative to account **a** (seen from **a**). */
typedef enum {
    FL_HERITAGE_REL_INVALID = 0,
    FL_HERITAGE_REL_SELF = 1,
    /** **b** is above **a** (a creator, or a creator's creator). */
    FL_HERITAGE_REL_ANCESTOR = 2,
    /** **b** is below **a**: **a** may act for **b**. */
    FL_HERITAGE_REL_DESCENDANT = 3,
    /** Same creator, different accounts. */
    FL_HERITAGE_REL_SIBLING = 4,
    /** Any other pair: different branches below a common ancestor. */
    FL_HERITAGE_REL_OTHER_BRANCH = 5
} fl_heritage_relation_t;

typedef enum {
    FL_BRIDGE_STATE_NONE = 0,
    /** Requested by one side; grants nothing yet. */
    FL_BRIDGE_STATE_PENDING = 1,
    /** Accepted by the other side (or an ancestor of it). */
    FL_BRIDGE_STATE_ACTIVE = 2
} fl_bridge_state_t;

_Static_assert(FL_HERITAGE_KEY_ID_MAX == 256u, "heritage key id buffer");

#endif /* FL_CONTRACT_P2_HERITAGE_H */

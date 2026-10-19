/**
 * Bridges between accounts on different heritage branches (contract_p2_heritage.h).
 *
 * Kept in the account store's SQLite file (table "bridges"). One side requests,
 * the other side or one of its ancestors accepts; either side or an ancestor of
 * either revokes. Only an active bridge opens anything, and only between its two
 * accounts.
 */
#ifndef FL_BRIDGE_H
#define FL_BRIDGE_H

#include "contract_result.h"
#include "contract_p2_heritage.h"
#include "user_db.h"
#include <stdint.h>

typedef struct fl_bridge {
    int64_t           id;
    /** Requesting account. */
    char              a_key[FL_HERITAGE_KEY_ID_MAX];
    /** Account asked to accept. */
    char              b_key[FL_HERITAGE_KEY_ID_MAX];
    fl_bridge_state_t state;
    /** Who accepted (b_key or an ancestor of it); empty while pending. */
    char              accepted_by[FL_HERITAGE_KEY_ID_MAX];
    int64_t           created_at;
    int64_t           accepted_at;
} fl_bridge_t;

/**
 * actor_key asks peer_key for a bridge. INVAL unless both are accounts in users
 * on different branches (sibling or other branch: one line needs no bridge);
 * BUSY when the pair already has a pending or active bridge.
 */
fl_result_t fl_bridge_request(const char *db_path, const fl_user_db_t *users,
                              const char *actor_key, const char *peer_key, int64_t now,
                              int64_t *id_out);

/** Activates a pending bridge. ACCES unless actor_key is b_key or an ancestor of it. */
fl_result_t fl_bridge_accept(const char *db_path, const char *actor_key, int64_t id, int64_t now);

/** Deletes a bridge (pending or active). ACCES unless actor_key is either side or an ancestor of one. */
fl_result_t fl_bridge_revoke(const char *db_path, const char *actor_key, int64_t id);

fl_result_t fl_bridge_get(const char *db_path, int64_t id, fl_bridge_t *out);

/** 1 when an active bridge joins exactly these two accounts (either order). */
int fl_bridge_active_between(const char *db_path, const char *a_key, const char *b_key);

typedef void (*fl_bridge_visit_fn)(const fl_bridge_t *bridge, void *ctx);

/** Visits bridges with an endpoint at or below actor_key (Root sees all). */
fl_result_t fl_bridge_list_visible(const char *db_path, const char *actor_key,
                                   fl_bridge_visit_fn visit, void *ctx);

/** Deletes every bridge naming key_id (the account is being removed). */
fl_result_t fl_bridge_remove_for(const char *db_path, const char *key_id);

#endif /* FL_BRIDGE_H */

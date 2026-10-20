/**
 * Bridges between accounts on different heritage branches (contract_p2_heritage.h).
 *
 * Kept in the account store's SQLite file (table "bridges"). One side requests,
 * the other side or one of its ancestors accepts; either side or an ancestor of
 * either revokes. Only an active bridge opens anything, and only between its two
 * accounts.
 *
 * Both halves are signed with the accounts' KeyQuorum keys (user_db.h): the
 * requester signs the request and the acceptor signs the acceptance, over the
 * two key ids, the signer and the bridge's random nonce. A bridge counts as
 * active only while both signatures verify under the signers' current keys, so
 * a row written without them (before signing existed) or changed by hand, or one
 * whose signer has since been given a new key, opens nothing.
 */
#ifndef FL_BRIDGE_H
#define FL_BRIDGE_H

#include "contract_result.h"
#include "contract_p2_heritage.h"
#include "user_db.h"
#include <stdint.h>

#define FL_BRIDGE_NONCE_LEN     16u
#define FL_BRIDGE_SIGNATURE_LEN 64u

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
    /** Hex: 16-byte random nonce, the requester's and the acceptor's Ed25519 signatures. */
    char              nonce_hex[2 * FL_BRIDGE_NONCE_LEN + 1];
    char              request_sig[2 * FL_BRIDGE_SIGNATURE_LEN + 1];
    char              accept_sig[2 * FL_BRIDGE_SIGNATURE_LEN + 1];
} fl_bridge_t;

/**
 * actor_key asks peer_key for a bridge, signed with actor_secret (the actor's
 * opened signing key, FL_USER_SIGNING_SECRET_LEN bytes). INVAL unless both are
 * accounts in users on different branches (sibling or other branch: one line
 * needs no bridge); ACCES when the actor has no key or actor_secret is not it;
 * BUSY when the pair already has a pending or active bridge.
 */
fl_result_t fl_bridge_request(const char *db_path, const fl_user_db_t *users,
                              const char *actor_key, const char *peer_key, int64_t now,
                              const unsigned char *actor_secret, int64_t *id_out);

/**
 * Activates a pending bridge, signed with actor_secret. ACCES unless actor_key
 * is b_key or an ancestor of it and actor_secret is its key.
 */
fl_result_t fl_bridge_accept(const char *db_path, const fl_user_db_t *users, const char *actor_key,
                             int64_t id, int64_t now, const unsigned char *actor_secret);

/** Deletes a bridge (pending or active). ACCES unless actor_key is either side or an ancestor of one. */
fl_result_t fl_bridge_revoke(const char *db_path, const char *actor_key, int64_t id);

fl_result_t fl_bridge_get(const char *db_path, int64_t id, fl_bridge_t *out);

/**
 * 1 when an active bridge joins exactly these two accounts (either order) and
 * both its signatures verify under the keys users holds (users->path is the store).
 */
int fl_bridge_active_between(const fl_user_db_t *users, const char *a_key, const char *b_key);

/** 1 when b is active and both its signatures verify under the keys users holds. */
int fl_bridge_verified(const fl_user_db_t *users, const fl_bridge_t *b);

typedef void (*fl_bridge_visit_fn)(const fl_bridge_t *bridge, void *ctx);

/** Visits bridges with an endpoint at or below actor_key (Root sees all). */
fl_result_t fl_bridge_list_visible(const char *db_path, const char *actor_key,
                                   fl_bridge_visit_fn visit, void *ctx);

/** Deletes every bridge naming key_id (the account is being removed). */
fl_result_t fl_bridge_remove_for(const char *db_path, const char *key_id);

#endif /* FL_BRIDGE_H */

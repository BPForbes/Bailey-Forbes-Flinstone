#ifndef FL_KQ_KEYDIR_H
#define FL_KQ_KEYDIR_H

/*
 * Where a KeyQuorum checkpoint (kq_checkpoint.h) finds an account's public keys. The
 * checkpoints read keys only through this base type; each account store extends it by
 * making it the first member of its own struct, as fl_kq_driver_t extends fl_driver_desc_t:
 *
 *   native shell   fl_user_db_keydir_t (kq_checkpoint_users.c) over the SQLite user store
 *   browser lab    the lab account table and the keys members publish on the lab server
 *
 * Both return NULL for a label they hold no key for; a checkpoint then refuses (NO_KEY) or
 * leaves that label's signatures out of the trust table.
 */

typedef struct fl_kq_keydir fl_kq_keydir_t;

struct fl_kq_keydir {
    /** The key id (label) an account name or a key id names, or NULL when there is none. */
    const char *(*resolve)(const fl_kq_keydir_t *dir, const char *who);
    /** The label's Ed25519 signing public key (32 bytes), or NULL. */
    const unsigned char *(*signing_public)(const fl_kq_keydir_t *dir, const char *label);
    /** The label's X25519 box public key (32 bytes), or NULL. */
    const unsigned char *(*box_public)(const fl_kq_keydir_t *dir, const char *label);
};

#endif /* FL_KQ_KEYDIR_H */

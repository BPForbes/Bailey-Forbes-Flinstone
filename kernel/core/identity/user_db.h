#ifndef FL_USER_DB_H
#define FL_USER_DB_H

#include "contract_result.h"
#include "contract_p2_credential_store.h"
#include "contract_p2_heritage.h"
#include "password_hash.h"
#include <stddef.h>
#include <stdint.h>

#define FL_USER_NAME_MAX          32u

_Static_assert(FL_USER_NAME_MAX <= FL_CREDENTIAL_USERNAME_MAX_CHARS + 1u,
               "user_db name cap must not exceed credential contract limit");
#define FL_PASSWORD_HASH_HEX_MAX  FL_PASSWORD_HASH_RECORD_MAX
#define FL_PASSWORD_SALT_HEX_MAX  33u
#define FL_USER_SIGNING_SECRET_LEN  32u
#define FL_USER_SIGNING_PUBLIC_LEN  32u
#define FL_USER_SIGNING_SEALED_LEN  81u

/** How an account proves itself at login. Every account that exists today is PASSWORD. */
#define FL_USER_AUTH_PASSWORD      0 /* the password alone (legacy) */
#define FL_USER_AUTH_PASSWORD_KQST 1 /* the password and the account's KQST slot token */

typedef struct fl_user_record {
    char     name[FL_USER_NAME_MAX];
    char     password_hash[FL_PASSWORD_HASH_HEX_MAX];
    char     salt_hex[FL_PASSWORD_SALT_HEX_MAX];
    uint32_t uid;
    int      is_elevated;
    /** Heritage key id (contract_p2_heritage.h): Root, Root.Flinstone, Root.Flinstone.Bob. */
    char     key_id[FL_HERITAGE_KEY_ID_MAX];
    /** Key id of the account that created this one; empty for Root. */
    char     created_by[FL_HERITAGE_KEY_ID_MAX];
    /** Wall-clock seconds at creation; 0 when unknown (accounts migrated from before heritage). */
    int64_t  created_at;
    /**
     * The account's KeyQuorum signing key: the Ed25519 public key, and the secret
     * sealed under the account's password (KQAK, keyquorum_identity.h). has_key is
     * 0 for an account that has not signed in since keys were introduced.
     */
    int           has_key;
    unsigned char signing_public[FL_USER_SIGNING_PUBLIC_LEN];
    unsigned char signing_sealed[FL_USER_SIGNING_SEALED_LEN];
    /**
     * The account's box key (X25519, kq_account_box_keygen): what co-signed file
     * letters are sealed to. Sealed under the password like the signing key and
     * created with it; has_box is 0 until the account next signs in.
     */
    int           has_box;
    unsigned char box_public[FL_USER_SIGNING_PUBLIC_LEN];
    unsigned char box_sealed[FL_USER_SIGNING_SEALED_LEN];
    /** FL_USER_AUTH_*. The KQST file's path is kept in the account_keys table, keyed by key_id. */
    int           auth_mode;
} fl_user_record_t;

#define FL_USER_DB_MAX_USERS 32u
#define FL_USERS_DB_DEFAULT_PATH FL_CREDENTIAL_STORE_LAB_SQLITE_REL

typedef struct fl_user_db {
    fl_user_record_t users[FL_USER_DB_MAX_USERS];
    size_t           count;
    char             default_user[FL_USER_NAME_MAX];
    char             path[256];
    int              loaded;
} fl_user_db_t;

/** Sets how an account logs in (in memory; fl_user_db_save writes it). NOENT for an unknown account. */
fl_result_t fl_user_db_set_auth_mode(fl_user_db_t *db, const char *name, int auth_mode);
/** Sets the in-memory elevation mark (fl_user_db_save writes it). NOENT for an unknown account. */
fl_result_t fl_user_db_set_elevated(fl_user_db_t *db, const char *name, int is_elevated);
/**
 * The KQST file path paired with key_id in the store's account_keys table (read at call time, so
 * a path never sits in the record). NOENT when the account has no row or the row has no path.
 */
fl_result_t fl_user_db_kqst_path(const fl_user_db_t *db, const char *key_id, char *out, size_t cap);
/**
 * Pairing facts the caller's registry holds for a KQST open: device id and the two public keys.
 * NOENT when the account_keys row is missing or any field is the wrong size / empty path alone.
 */
fl_result_t fl_user_db_kqst_pairing(const fl_user_db_t *db, const char *key_id,
                                    unsigned char device_id[16], unsigned char signing_public[32],
                                    unsigned char encryption_public[32]);

/*
 * Private bridges (KeyQuorum's, mirrored in the account store's account_bridges and
 * account_bridge_members tables; not the account-to-account bridges of bridge.c). The store is the
 * caller's own trusted record, read at call time like account_keys: a bridge is live when its row
 * says destroyed = 0 and its facts are well formed (generation 1..UINT32_MAX, a 16-byte salt, a
 * 32-byte public key, at least two members with valid heritage labels and 32-byte signing keys).
 * No import path writes these rows yet (bringing a KeyQuorum bridge into the kernel side is not
 * designed); fl_user_db_bridge_put / _put_sealed_key are the store's writers.
 */
#define FL_USER_BRIDGE_UID_MAX     65u /* KeyQuorum's uid (32 hex) and its NUL, with room */
#define FL_USER_BRIDGE_MEMBERS_MAX 16u
#define FL_USER_BRIDGES_MAX        16u
#define FL_USER_BRIDGE_SALT_LEN    16u
#define FL_USER_BRIDGE_SEALED_MAX  256u

typedef struct fl_user_bridge_member {
    char          label[FL_HERITAGE_KEY_ID_MAX];
    unsigned char encryption_public[32];
    unsigned char signing_public[32];
} fl_user_bridge_member_t;

typedef struct fl_user_bridge {
    char                    uid[FL_USER_BRIDGE_UID_MAX];
    uint32_t                generation;
    unsigned char           salt[FL_USER_BRIDGE_SALT_LEN];
    unsigned char           public_key[32];
    int                     destroyed;
    size_t                  nmembers;
    fl_user_bridge_member_t members[FL_USER_BRIDGE_MEMBERS_MAX];
} fl_user_bridge_t;

/** Writes a bridge's facts and its member roster (replacing the row and its members' keys). */
fl_result_t fl_user_db_bridge_put(const fl_user_db_t *db, const fl_user_bridge_t *bridge);
/**
 * fl_user_db_bridge_put plus member label's sealed bridge key and wrap salt, in ONE transaction: the
 * roster and the key land together or not at all. INVAL for a malformed bridge or key, NOENT when label is
 * not on the roster (nothing is written).
 */
fl_result_t fl_user_db_bridge_put_with_key(const fl_user_db_t *db, const fl_user_bridge_t *bridge, const char *label,
                                           const unsigned char *sealed, size_t len,
                                           const unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);

/**
 * Copies bridge uid from a KeyQuorum store (the SQLite file `keyquorum private-bridge import` /
 * `inbox open` writes; opened read-only, one snapshot) into this account store. local_label, when given,
 * is the account whose sealed bridge key and wrap salt come with it (KeyQuorum holds only a local member's);
 * NULL imports the roster alone, which is enough to verify but not to prove. NOENT: no such bridge, the
 * label is not on its roster, or KeyQuorum holds no sealed key for it. NOMEM: a roster larger than
 * FL_USER_BRIDGE_MEMBERS_MAX (never truncated). ERR: any column of the wrong type or size. A bridge
 * KeyQuorum marks destroyed is stored destroyed. Validates no package and unseals nothing.
 */
fl_result_t fl_user_db_bridge_import_from_kq(const fl_user_db_t *db, const char *kq_store_path, const char *uid,
                                             const char *local_label);

/**
 * Stores member label's sealed copy of the bridge secret together with its wrap salt (one statement:
 * neither is ever written alone). NOENT when the member row does not exist.
 */
fl_result_t fl_user_db_bridge_put_sealed_key(const fl_user_db_t *db, const char *uid, const char *label,
                                             const unsigned char *sealed, size_t len,
                                             const unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);
/**
 * The bridge uid's current facts as the store holds them now (destroyed included, so the caller
 * passes them to the verifier unchanged). The row and its roster are one deferred snapshot.
 * NOENT for an unknown or malformed row; ERR when the store cannot be read.
 */
fl_result_t fl_user_db_bridge_get(const fl_user_db_t *db, const char *uid, fl_user_bridge_t *out);
/**
 * Every live bridge (see above) into out[0..cap), in uid order; *count is how many. The uid list
 * and each roster are one deferred snapshot, so a row cannot flip to destroyed between them.
 * Never a partial list: a row with destroyed = 0 that is malformed or cannot be read, or any
 * failed read, is ERR, and more live rows than cap is NOMEM (*count 0 on both). Only a store
 * whose schema has no account_bridges table (written before the bridge tables) is OK with no bridge.
 */
fl_result_t fl_user_db_bridges_live(const fl_user_db_t *db, fl_user_bridge_t *out, size_t cap,
                                    size_t *count);
/**
 * Member label's sealed bridge key and the wrap salt stored with it. NOENT unless both are present
 * and the right sizes (a sealed key without its salt, or a salt without its key, opens nothing).
 * *len is the sealed key's length. The caller wipes nothing secret: the key is sealed.
 */
fl_result_t fl_user_db_bridge_sealed_key(const fl_user_db_t *db, const char *uid, const char *label,
                                         unsigned char *sealed, size_t cap, size_t *len,
                                         unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);
/**
 * Facts and one member's sealed key from one read-only deferred snapshot. A missing or malformed
 * bridge leaves *out zeroed. A bridge that reads and whose sealed key does not is returned with
 * the facts still filled, so the caller can tell the two apart. Prove uses this; the two public
 * reads above are each their own snapshot.
 */
fl_result_t fl_user_db_bridge_snapshot(const fl_user_db_t *db, const char *uid, const char *label,
                                       fl_user_bridge_t *out, unsigned char *sealed, size_t cap,
                                       size_t *len,
                                       unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);

/** Writer reservation over the account database. The body is not part of the header. */
typedef struct fl_user_bridge_hold fl_user_bridge_hold_t;
/**
 * Read-write open, busy_timeout 0, BEGIN IMMEDIATE. Holds the SQLite writer slot until
 * fl_user_db_bridge_release, which rolls the transaction back (it writes nothing). BUSY when
 * another writer already holds the database; ACCES when it cannot be reserved for any other
 * reason. The same account_bridges tables as the public readers.
 */
fl_result_t fl_user_db_bridge_reserve(const fl_user_db_t *db, fl_user_bridge_hold_t **out);
void        fl_user_db_bridge_release(fl_user_bridge_hold_t *hold);
/** Live bridges on a reserved connection. Same results as fl_user_db_bridges_live. */
fl_result_t fl_user_db_bridge_live_held(fl_user_bridge_hold_t *hold, fl_user_bridge_t *out, size_t cap,
                                        size_t *count);
/**
 * Facts on a reserved connection, and when label is non-NULL that member's sealed key, as one
 * snapshot. A missing sealed key leaves *out filled.
 */
fl_result_t fl_user_db_bridge_read_held(fl_user_bridge_hold_t *hold, const char *uid, const char *label,
                                        fl_user_bridge_t *out, unsigned char *sealed, size_t cap,
                                        size_t *len,
                                        unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path);
fl_result_t fl_user_db_save(const fl_user_db_t *db);
void        fl_user_db_clear(fl_user_db_t *db);
void        fl_user_db_seed_defaults(fl_user_db_t *db);

/** Account names match case-insensitively ("root" finds Root); the stored spelling is canonical. */
const fl_user_record_t *fl_user_db_find(const fl_user_db_t *db, const char *name);
const fl_user_record_t *fl_user_db_find_by_key(const fl_user_db_t *db, const char *key_id);
int         fl_user_db_verify_password(const fl_user_db_t *db, const char *name,
                                       const char *password);
int         fl_user_db_is_elevated_user(const fl_user_db_t *db, const char *name);

#define FL_USER_DB_MIN_ALLOC_UID 2000u

/** Adds an account under Root (key Root.<name>), or Root itself when name is "Root". */
fl_result_t fl_user_db_add_user(fl_user_db_t *db, const char *name, const char *password,
                                uint32_t uid, int is_elevated);
/**
 * Adds an account created by creator_key: its key id is creator_key "." name.
 * FL_RESULT_INVAL for a name outside [A-Za-z0-9_-]{1,31} or "Root", NOENT when
 * creator_key names no account, BUSY for a taken name (any case) or uid.
 */
fl_result_t fl_user_db_add_user_under(fl_user_db_t *db, const char *creator_key, const char *name,
                                      const char *password, uint32_t uid, int is_elevated,
                                      int64_t created_at);
/** 1 when some other account's key id descends from key_id. */
int         fl_user_db_has_descendants(const fl_user_db_t *db, const char *key_id);
/** Next non-root UID not already assigned (starts at **FL_USER_DB_MIN_ALLOC_UID**). */
uint32_t    fl_user_db_next_uid(const fl_user_db_t *db);
/** Refuses Root (ACCES) and an account that still has descendants (BUSY). */
fl_result_t fl_user_db_remove_user(fl_user_db_t *db, const char *name);
/**
 * Sets a new password without the old one (an ancestor resetting it). The
 * account gets a new signing key, so bridges it signed no longer verify and
 * must be signed again.
 */
fl_result_t fl_user_db_set_password(fl_user_db_t *db, const char *name, const char *password);
/**
 * Changes a password with the current one: the signing key is kept and sealed
 * again under the new password (or created, when the account had none). ACCES
 * when the current password is wrong.
 */
fl_result_t fl_user_db_change_password(fl_user_db_t *db, const char *name,
                                       const char *current, const char *password);

/* What fl_user_db_ensure_key created (0: nothing). */
#define FL_USER_DB_CREATED_KEYS 1 /* a signing key and a box key */
#define FL_USER_DB_CREATED_BOX  2 /* a box key beside an existing signing key */

/**
 * Gives an account that has no signing key one (and a box key), or an account
 * with a signing key but no box key a box key, sealed under password, which
 * must be the account's password. FL_RESULT_OK when it already had both; the
 * caller saves the store when *created is set, and undoes it with
 * fl_user_db_forget_created when the save fails.
 */
fl_result_t fl_user_db_ensure_key(fl_user_db_t *db, const char *name, const char *password,
                                  int *created);
/**
 * Undoes exactly what fl_user_db_ensure_key reported in created: both keys for
 * FL_USER_DB_CREATED_KEYS, only the box key for FL_USER_DB_CREATED_BOX, so an
 * existing signing key (and the bridges and histories it signed) survives a
 * failed save.
 */
void        fl_user_db_forget_created(fl_user_db_t *db, const char *name, int created);
/**
 * Drops the signing key of the account named name from memory (has_key 0).
 * Used to undo fl_user_db_ensure_key when the store could not be saved, so no
 * signature is ever made with a key the store does not hold.
 */
void        fl_user_db_forget_key(fl_user_db_t *db, const char *name);
/**
 * Opens the signing key of the account named name with its password into secret
 * (FL_USER_SIGNING_SECRET_LEN bytes). ACCES for a wrong password, NOENT when the
 * account has no key. The caller wipes secret; once the open was attempted, every non-OK return
 * leaves it securely zeroed.
 */
/**
 * The same open, from a copy of the account's record: for a caller that took the copy under a lock and
 * runs the slow key derivation outside it (the session hands the key service such a copy at login).
 */
fl_result_t fl_user_db_open_key_record(const fl_user_record_t *account, const char *password,
                                      unsigned char *secret);
/** The box-key open from a record copy, same as fl_user_db_open_key_record. */
fl_result_t fl_user_db_open_box_record(const fl_user_record_t *account, const char *password,
                                      unsigned char *secret);
fl_result_t fl_user_db_open_key(const fl_user_db_t *db, const char *name, const char *password,
                                unsigned char *secret);
/** The signing public key of key_id, or NULL when the account has none. */
const unsigned char *fl_user_db_signing_public(const fl_user_db_t *db, const char *key_id);
/**
 * Opens the box key of the account named name with its password into secret
 * (FL_USER_SIGNING_SECRET_LEN bytes). ACCES for a wrong password, NOENT when the
 * account has no box key. The caller wipes secret; once the open was attempted, every non-OK return
 * leaves it securely zeroed.
 */
fl_result_t fl_user_db_open_box(const fl_user_db_t *db, const char *name, const char *password,
                                unsigned char *secret);
/** The box public key of key_id, or NULL when the account has none. */
const unsigned char *fl_user_db_box_public(const fl_user_db_t *db, const char *key_id);

typedef enum { FL_USER_SECRET_SIGNING, FL_USER_SECRET_BOX } fl_user_secret_kind_t;
/**
 * 1 only when the account with key_id holds a key of kind and secret
 * (FL_USER_SIGNING_SECRET_LEN bytes) derives exactly its stored public key; 0 for NULL
 * arguments, an unknown key_id, no key of that kind, a derive failure or a mismatch.
 * Constant time in the secret. Takes no password and does not wipe secret: the caller
 * wipes its own secret.
 */
int fl_user_db_secret_matches(const fl_user_db_t *db, const char *key_id,
                              fl_user_secret_kind_t kind, const unsigned char *secret);

#endif /* FL_USER_DB_H */

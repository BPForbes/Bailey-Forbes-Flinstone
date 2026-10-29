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

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

#define FL_USER_AUTH_PASSWORD      0 /* the password alone (legacy) */
#define FL_USER_AUTH_PASSWORD_KQST 1 /* the password and the account's KQST slot token */

typedef struct fl_user_record {
    char     name[FL_USER_NAME_MAX];
    char     password_hash[FL_PASSWORD_HASH_HEX_MAX];
    char     salt_hex[FL_PASSWORD_SALT_HEX_MAX];
    uint32_t uid;
    int      is_elevated;
    char     key_id[FL_HERITAGE_KEY_ID_MAX];
    char     created_by[FL_HERITAGE_KEY_ID_MAX];
    int64_t  created_at;
    /* Sealed under the account password; has_key and has_box stay 0 until the next sign-in. */
    int           has_key;
    unsigned char signing_public[FL_USER_SIGNING_PUBLIC_LEN];
    unsigned char signing_sealed[FL_USER_SIGNING_SEALED_LEN];
    int           has_box;
    unsigned char box_public[FL_USER_SIGNING_PUBLIC_LEN];
    unsigned char box_sealed[FL_USER_SIGNING_SEALED_LEN];
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

fl_result_t fl_user_db_set_auth_mode(fl_user_db_t *db, const char *name, int auth_mode);
fl_result_t fl_user_db_kqst_path(const fl_user_db_t *db, const char *key_id, char *out, size_t cap);
    /* NOENT when the account_keys row or any pairing field is missing or the wrong size. */
fl_result_t fl_user_db_kqst_pairing(const fl_user_db_t *db, const char *key_id,
                                    unsigned char device_id[16], unsigned char signing_public[32],
                                    unsigned char encryption_public[32]);

#define FL_USER_KQST_PATH_MAX   512u
#define FL_USER_KQST_TEXT_MAX   128u
#define FL_USER_KQST_HASH_MAX   64u

typedef struct fl_user_kqst_row {
    int           present;
    char          purpose[FL_USER_KQST_TEXT_MAX];
    int64_t       holds_share;
    char          minted_by[FL_USER_KQST_TEXT_MAX];
    int64_t       minted_at;
    int64_t       generation;
    int           has_path;
    char          kqst_path[FL_USER_KQST_PATH_MAX];
    unsigned char device_id[16];
    unsigned char signing_public[32];
    unsigned char encryption_public[32];
    int64_t       ledger_seq;
    unsigned char event_hash[FL_USER_KQST_HASH_MAX];
    size_t        event_hash_len;
} fl_user_kqst_row_t;

fl_result_t fl_user_db_kqst_register(const fl_user_db_t *db, const char *key_id, const char *kqst_path,
                                     const unsigned char device_id[16],
                                     const unsigned char signing_public[32],
                                     const unsigned char encryption_public[32], int64_t minted_at,
                                     int64_t *generation_out);
fl_result_t fl_user_db_kqst_row_get(const fl_user_db_t *db, const char *key_id, fl_user_kqst_row_t *out);
fl_result_t fl_user_db_kqst_row_restore(const fl_user_db_t *db, const char *key_id,
                                        const fl_user_kqst_row_t *row);

#define FL_USER_BRIDGE_UID_MAX     65u
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

fl_result_t fl_user_db_bridge_put(const fl_user_db_t *db, const fl_user_bridge_t *bridge);
fl_result_t fl_user_db_bridge_put_with_key(const fl_user_db_t *db, const fl_user_bridge_t *bridge, const char *label,
                                           const unsigned char *sealed, size_t len,
                                           const unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);

/* NOENT, NOMEM (never truncated), ERR, BUSY (a newer generation is held). */
fl_result_t fl_user_db_bridge_import_from_kq(const fl_user_db_t *db, const char *kq_store_path, const char *uid,
                                             const char *local_label);

fl_result_t fl_user_db_bridge_put_sealed_key(const fl_user_db_t *db, const char *uid, const char *label,
                                             const unsigned char *sealed, size_t len,
                                             const unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);
fl_result_t fl_user_db_bridge_get(const fl_user_db_t *db, const char *uid, fl_user_bridge_t *out);
fl_result_t fl_user_db_bridges_live(const fl_user_db_t *db, fl_user_bridge_t *out, size_t cap,
                                    size_t *count);
fl_result_t fl_user_db_bridge_sealed_key(const fl_user_db_t *db, const char *uid, const char *label,
                                         unsigned char *sealed, size_t cap, size_t *len,
                                         unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);
fl_result_t fl_user_db_bridge_snapshot(const fl_user_db_t *db, const char *uid, const char *label,
                                       fl_user_bridge_t *out, unsigned char *sealed, size_t cap,
                                       size_t *len,
                                       unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);

typedef struct fl_user_bridge_hold fl_user_bridge_hold_t;
/* BUSY when another writer holds the database; release rolls back. */
fl_result_t fl_user_db_bridge_reserve(const fl_user_db_t *db, fl_user_bridge_hold_t **out);
void        fl_user_db_bridge_release(fl_user_bridge_hold_t *hold);
fl_result_t fl_user_db_bridge_live_held(fl_user_bridge_hold_t *hold, fl_user_bridge_t *out, size_t cap,
                                        size_t *count);
fl_result_t fl_user_db_bridge_read_held(fl_user_bridge_hold_t *hold, const char *uid, const char *label,
                                        fl_user_bridge_t *out, unsigned char *sealed, size_t cap,
                                        size_t *len,
                                        unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]);

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path);
fl_result_t fl_user_db_save(const fl_user_db_t *db);
void        fl_user_db_clear(fl_user_db_t *db);
void        fl_user_db_seed_defaults(fl_user_db_t *db);

const fl_user_record_t *fl_user_db_find(const fl_user_db_t *db, const char *name);
const fl_user_record_t *fl_user_db_find_by_key(const fl_user_db_t *db, const char *key_id);
int         fl_user_db_verify_password(const fl_user_db_t *db, const char *name,
                                       const char *password);
int         fl_user_db_is_elevated_user(const fl_user_db_t *db, const char *name);

#define FL_USER_DB_MIN_ALLOC_UID 2000u

fl_result_t fl_user_db_add_user(fl_user_db_t *db, const char *name, const char *password,
                                uint32_t uid, int is_elevated);
fl_result_t fl_user_db_add_user_under(fl_user_db_t *db, const char *creator_key, const char *name,
                                      const char *password, uint32_t uid, int is_elevated,
                                      int64_t created_at);
int         fl_user_db_has_descendants(const fl_user_db_t *db, const char *key_id);
uint32_t    fl_user_db_next_uid(const fl_user_db_t *db);
fl_result_t fl_user_db_remove_user(fl_user_db_t *db, const char *name);
    /* Sets a password without the old one: the account gets a new signing key, so its bridges must be signed again. */
fl_result_t fl_user_db_set_password(fl_user_db_t *db, const char *name, const char *password);
fl_result_t fl_user_db_change_password(fl_user_db_t *db, const char *name,
                                       const char *current, const char *password);

#define FL_USER_DB_CREATED_KEYS 1 /* a signing key and a box key */
#define FL_USER_DB_CREATED_BOX  2 /* a box key beside an existing signing key */

fl_result_t fl_user_db_ensure_key(fl_user_db_t *db, const char *name, const char *password,
                                  int *created);
    /* Undoes what ensure_key created, so an existing signing key survives a failed save. */
void        fl_user_db_forget_created(fl_user_db_t *db, const char *name, int created);
void        fl_user_db_forget_key(fl_user_db_t *db, const char *name);
fl_result_t fl_user_db_open_key_record(const fl_user_record_t *account, const char *password,
                                      unsigned char *secret);
fl_result_t fl_user_db_open_box_record(const fl_user_record_t *account, const char *password,
                                      unsigned char *secret);
    /* The caller wipes secret; after the attempt, every non-OK return leaves it zeroed. */
fl_result_t fl_user_db_open_key(const fl_user_db_t *db, const char *name, const char *password,
                                unsigned char *secret);
const unsigned char *fl_user_db_signing_public(const fl_user_db_t *db, const char *key_id);
fl_result_t fl_user_db_open_box(const fl_user_db_t *db, const char *name, const char *password,
                                unsigned char *secret);
const unsigned char *fl_user_db_box_public(const fl_user_db_t *db, const char *key_id);

typedef enum { FL_USER_SECRET_SIGNING, FL_USER_SECRET_BOX } fl_user_secret_kind_t;
    /* Constant time in the secret; takes no password and does not wipe secret. */
int fl_user_db_secret_matches(const fl_user_db_t *db, const char *key_id,
                              fl_user_secret_kind_t kind, const unsigned char *secret);

#define FL_USER_ASSEMBLY_MAX 16384u
fl_result_t fl_user_db_assembly_put(const fl_user_db_t *db, const char *parent_label, unsigned generation,
                                    unsigned threshold, const unsigned char *bytes, size_t len);
fl_result_t fl_user_db_assembly_get(const fl_user_db_t *db, const char *parent_label, unsigned char *out,
                                    size_t cap, size_t *len, unsigned *generation, unsigned *threshold);

#endif /* FL_USER_DB_H */

#ifndef FL_USER_DB_H
#define FL_USER_DB_H

#include "contract_result.h"
#include "password_hash.h"
#include <stddef.h>
#include <stdint.h>

#define FL_USER_NAME_MAX          32u
#define FL_PASSWORD_HASH_HEX_MAX  FL_PASSWORD_HASH_RECORD_MAX
#define FL_PASSWORD_SALT_HEX_MAX  33u

typedef struct fl_user_record {
    char     name[FL_USER_NAME_MAX];
    char     password_hash[FL_PASSWORD_HASH_HEX_MAX];
    char     salt_hex[FL_PASSWORD_SALT_HEX_MAX];
    uint32_t uid;
    int      is_elevated;
} fl_user_record_t;

#define FL_USER_DB_MAX_USERS 32u
#define FL_USERS_DB_DEFAULT_PATH "userland/shell/fl_users.db"

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

const fl_user_record_t *fl_user_db_find(const fl_user_db_t *db, const char *name);
int         fl_user_db_verify_password(const fl_user_db_t *db, const char *name,
                                       const char *password);
int         fl_user_db_is_elevated_user(const fl_user_db_t *db, const char *name);

fl_result_t fl_user_db_add_user(fl_user_db_t *db, const char *name, const char *password,
                                uint32_t uid, int is_elevated);
fl_result_t fl_user_db_remove_user(fl_user_db_t *db, const char *name);
fl_result_t fl_user_db_set_password(fl_user_db_t *db, const char *name, const char *password);

#endif /* FL_USER_DB_H */

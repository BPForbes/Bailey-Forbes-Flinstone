#ifndef FL_USER_DB_H
#define FL_USER_DB_H

#include "contract_result.h"
#include <stddef.h>
#include <stdint.h>

#define FL_USER_NAME_MAX 32u

typedef struct fl_user_record {
    char     name[FL_USER_NAME_MAX];
    uint32_t uid;
    int      is_root;
} fl_user_record_t;

#define FL_USER_DB_MAX_USERS 32u

typedef struct fl_user_db {
    fl_user_record_t users[FL_USER_DB_MAX_USERS];
    size_t           count;
    char             default_user[FL_USER_NAME_MAX];
    int              loaded;
} fl_user_db_t;

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path);
void        fl_user_db_clear(fl_user_db_t *db);
const fl_user_record_t *fl_user_db_find(const fl_user_db_t *db, const char *name);
int         fl_user_db_is_root_user(const fl_user_db_t *db, const char *name);

#endif /* FL_USER_DB_H */

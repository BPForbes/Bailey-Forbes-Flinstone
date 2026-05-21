#ifndef FL_SESSION_H
#define FL_SESSION_H

#include "user_db.h"
#include "contract_p2_elevation.h"

#define FL_SESSION_USERS_PATH FL_USERS_DB_DEFAULT_PATH

void        fl_session_init(void);
const char *fl_session_current_user(void);

/** Logged-in account has is_elevated (root-like): no sudo for protected ops or jail. */
int         fl_session_is_elevated_account(void);

/** Elevated account or active sudo elevation token. */
int         fl_session_has_elevation(void);

fl_result_t fl_session_login(const char *name, const char *password);
fl_result_t fl_session_set_user(const char *name);

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out);
void        fl_session_bind_elevation(fl_elevation_token_t token);
void        fl_session_clear_elevation(void);

fl_user_db_t *fl_session_user_db_mut(void);
const fl_user_db_t *fl_session_user_db(void);
fl_result_t fl_session_save_users(void);

#endif /* FL_SESSION_H */

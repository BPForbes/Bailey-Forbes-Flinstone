#ifndef FL_SESSION_H
#define FL_SESSION_H

#include "user_db.h"
#include "contract_p2_elevation.h"

#define FL_SESSION_USERS_PATH FL_USERS_DB_DEFAULT_PATH

void        fl_session_init(void);
const char *fl_session_current_user(void);

/** Logged-in account has is_elevated (root-like). */
int         fl_session_is_elevated_account(void);

/** Elevated account, active sudo elevation token, or sudo command scope. */
int         fl_session_has_elevation(void);

/** VM jail escape: elevated account or sudo command scope (not token-only). */
int         fl_session_jail_privileged(void);

int         fl_session_in_sudo_scope(void);
void        fl_session_begin_sudo_scope(void);
void        fl_session_end_sudo_scope(void);

fl_result_t fl_session_login(const char *name, const char *password);
fl_result_t fl_session_set_user(const char *name);

/** Verify password for the current session user (sudo-style self-auth). */
int         fl_session_verify_password(const char *password);

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out);
void        fl_session_bind_elevation(fl_elevation_token_t token);
void        fl_session_clear_elevation(void);

/** Revoke active elevation token and end sudo command scope; keep current user. */
void        fl_session_drop_elevation(void);

/** Drop elevation and switch to the database default user (logout). */
fl_result_t fl_session_logout(void);

fl_user_db_t *fl_session_user_db_mut(void);
const fl_user_db_t *fl_session_user_db(void);
fl_result_t fl_session_save_users(void);

#endif /* FL_SESSION_H */

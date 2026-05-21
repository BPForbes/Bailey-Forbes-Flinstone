#ifndef FL_SESSION_H
#define FL_SESSION_H

#include "user_db.h"
#include "contract_p2_elevation.h"

#define FL_SESSION_USERS_PATH "userland/shell/users.lab.json"

void        fl_session_init(void);
const char *fl_session_current_user(void);
int         fl_session_is_root(void);
int         fl_session_has_elevation(void);
fl_result_t fl_session_set_user(const char *name);
fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out);
void        fl_session_bind_elevation(fl_elevation_token_t token);
const fl_user_db_t *fl_session_user_db(void);

#endif /* FL_SESSION_H */

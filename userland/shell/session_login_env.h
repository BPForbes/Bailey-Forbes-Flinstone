#ifndef FL_SESSION_LOGIN_ENV_H
#define FL_SESSION_LOGIN_ENV_H

#include <stddef.h>

/** Apply login-shell cwd/HOME for **su -** / **sudo -i** (hosted lab). Returns 0 on success. */
int fl_session_apply_login_shell_env(const char *username);

/** Snapshot current login-shell HOME/cwd (hosted lab). */
void fl_session_save_login_shell_env(char *home_out, size_t home_sz, char *cwd_out, size_t cwd_sz);

/** Restore a snapshot from fl_session_save_login_shell_env. */
void fl_session_restore_login_shell_env(const char *home, const char *cwd);

#endif

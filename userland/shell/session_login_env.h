#ifndef FL_SESSION_LOGIN_ENV_H
#define FL_SESSION_LOGIN_ENV_H

/** Apply login-shell cwd/HOME for **su -** / **sudo -i** (hosted lab). */
void fl_session_apply_login_shell_env(const char *username);

#endif

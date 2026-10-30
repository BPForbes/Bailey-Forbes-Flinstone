#include "session_login_env.h"
#include "common.h"
#include "fs_jail.h"
#include "session.h"
#include "user_db.h"
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* The home of the account running the shell, from the password database: never from $HOME,
 * which whoever started the process sets, so no path the shell opens is rooted at it. */
static int passwd_home(char *home, size_t home_sz) {
    struct passwd pw;
    struct passwd *res = NULL;
    char buf[1024];

    if (getpwuid_r(geteuid(), &pw, buf, sizeof buf, &res) != 0 || !res || !res->pw_dir ||
        res->pw_dir[0] != '/' || strlen(res->pw_dir) >= home_sz)
        return -1;
    memcpy(home, res->pw_dir, strlen(res->pw_dir) + 1u);
    return 0;
}

static void login_home_for_user(const char *username, char *home, size_t home_sz) {
    if (!username || !username[0] || !home || home_sz == 0u)
        return;

    if (fs_jail_is_active()) {
        /* Inside the jail every account has its own home: <jail>/home/Flinstone/Bob. */
        const fl_user_record_t *u = fl_user_db_find(fl_session_user_db(), username);

        if (u && fs_jail_account_home(u->key_id, home, home_sz) == 0)
            return;
    }
    if (fs_jail_is_active() && g_cwd[0]) {
        strncpy(home, g_cwd, home_sz - 1u);
        home[home_sz - 1u] = '\0';
        return;
    }
    if (!strcasecmp(username, "root")) {
        strncpy(home, "/", home_sz - 1u);
        home[home_sz - 1u] = '\0';
        return;
    }
    if (!strcmp(username, "guest")) {
        strncpy(home, "/tmp", home_sz - 1u);
        home[home_sz - 1u] = '\0';
        return;
    }
    if (passwd_home(home, home_sz) == 0)
        return;
    strncpy(home, ".", home_sz - 1u);
    home[home_sz - 1u] = '\0';
}

void fl_session_save_login_shell_env(char *home_out, size_t home_sz, char *cwd_out, size_t cwd_sz) {
    const char *home = getenv("HOME");

    if (home_out && home_sz > 0u) {
        if (home && home[0])
            strncpy(home_out, home, home_sz - 1u);
        else
            strncpy(home_out, ".", home_sz - 1u);
        home_out[home_sz - 1u] = '\0';
    }
    if (cwd_out && cwd_sz > 0u) {
        strncpy(cwd_out, g_cwd, cwd_sz - 1u);
        cwd_out[cwd_sz - 1u] = '\0';
    }
}

void fl_session_restore_login_shell_env(const char *home, const char *cwd) {
    if (home && home[0])
        (void)setenv("HOME", home, 1);
    if (cwd && cwd[0]) {
        if (chdir(cwd) != 0)
            perror("login-shell chdir");
        else {
            strncpy(g_cwd, cwd, sizeof(g_cwd) - 1u);
            g_cwd[sizeof(g_cwd) - 1u] = '\0';
        }
    }
}

int fl_session_apply_login_shell_env(const char *username) {
    char home[CWD_MAX];

    login_home_for_user(username, home, sizeof(home));
    (void)setenv("HOME", home, 1);
    strncpy(g_cwd, home, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
    if (chdir(g_cwd) != 0) {
        perror("login-shell chdir");
        return -1;
    }
    return 0;
}

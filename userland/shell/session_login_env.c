#include "session_login_env.h"
#include "common.h"
#include "fs_jail.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void login_home_for_user(const char *username, char *home, size_t home_sz) {
    if (!username || !username[0] || !home || home_sz == 0u)
        return;

    if (fs_jail_is_active() && g_cwd[0]) {
        strncpy(home, g_cwd, home_sz - 1u);
        home[home_sz - 1u] = '\0';
        return;
    }
    if (!strcmp(username, "root")) {
        strncpy(home, "/", home_sz - 1u);
        home[home_sz - 1u] = '\0';
        return;
    }
    if (!strcmp(username, "guest")) {
        strncpy(home, "/tmp", home_sz - 1u);
        home[home_sz - 1u] = '\0';
        return;
    }
    {
        const char *env = getenv("HOME");
        if (env && env[0]) {
            strncpy(home, env, home_sz - 1u);
            home[home_sz - 1u] = '\0';
            return;
        }
    }
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
        strncpy(g_cwd, cwd, sizeof(g_cwd) - 1u);
        g_cwd[sizeof(g_cwd) - 1u] = '\0';
        if (chdir(g_cwd) != 0)
            perror("login-shell chdir");
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

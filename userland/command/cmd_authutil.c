#include "cmd_authutil.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "user_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

void cmd_wipe_password(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0)
        return;
    memset(buf, 0, buf_size);
}

int cmd_read_password(const char *prompt, char *buf, size_t buf_size) {
    const char *env_pw;
    char *got;
    const char *line_prompt = "Password: ";

    if (!buf || buf_size < 2)
        return -1;

    env_pw = getenv("FL_CMD_PASSWORD");
    if (env_pw && env_pw[0]) {
        strncpy(buf, env_pw, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return 0;
    }

    if (!prompt || !prompt[0])
        prompt = line_prompt;

#if defined(__unix__) || defined(__APPLE__)
    fputs("\n", stderr);
    fflush(stderr);
    got = getpass(prompt);
    if (!got)
        return -1;
    strncpy(buf, got, buf_size - 1);
    buf[buf_size - 1] = '\0';
    return 0;
#else
    fputs("\n", stderr);
    fputs(prompt, stderr);
    fflush(stderr);
    if (!fgets(buf, (int)buf_size, stdin))
        return -1;
    {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = '\0';
    }
    return 0;
#endif
}

int cmd_sudo_require_auth(void) {
    char pw[128];
    fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
    fl_result_t rc;

    fl_session_init();
    if (fl_session_is_elevated_account())
        return 0;
    if (fl_session_has_elevation())
        return 0;

    if (cmd_read_password("[sudo] password: ", pw, sizeof(pw)) != 0) {
        fprintf(stderr, "sudo: password read failed\n");
        return 1;
    }
    if (!fl_session_verify_password(pw)) {
        cmd_wipe_password(pw, sizeof(pw));
        fprintf(stderr, "sudo: incorrect password\n");
        return 1;
    }
    cmd_wipe_password(pw, sizeof(pw));

    rc = fl_session_grant_elevation("sudo authentication", &tok);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo: elevation failed (%d)\n", (int)rc);
        return 1;
    }
    return 0;
}

int cmd_su_require_auth(const char *target) {
    char pw[128];
    const fl_user_db_t *db;

    if (!target || !target[0]) {
        fprintf(stderr, "su: invalid target user\n");
        return 1;
    }

    fl_session_init();
    db = fl_session_user_db();
    if (!fl_user_db_find(db, target)) {
        fprintf(stderr, "su: user %s does not exist\n", target);
        return 1;
    }

    if (cmd_read_password("Password: ", pw, sizeof(pw)) != 0) {
        fprintf(stderr, "su: password read failed\n");
        return 1;
    }

    if (!fl_user_db_verify_password(db, target, pw)) {
        cmd_wipe_password(pw, sizeof(pw));
        fprintf(stderr, "su: authentication failed\n");
        return 1;
    }
    cmd_wipe_password(pw, sizeof(pw));
    return 0;
}

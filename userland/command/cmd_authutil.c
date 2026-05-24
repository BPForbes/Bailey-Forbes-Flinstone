#include "cmd_authutil.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "user_db.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

void cmd_wipe_password(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0)
        return;
    memset(buf, 0, buf_size);
}

static int cmd_read_password_line(const char *prompt, char *buf, size_t buf_size) {
    (void)prompt;
    {
        int read_size = (buf_size > (size_t)INT_MAX) ? INT_MAX : (int)buf_size;
        if (!fgets(buf, read_size, stdin))
            return -1;
    }
    {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = '\0';
    }
    return 0;
}

#if defined(__unix__) || defined(__APPLE__)
static int cmd_read_password_tty(const char *prompt, char *buf, size_t buf_size) {
    struct termios old_tty;
    struct termios new_tty;
    int rc = -1;

    if (!isatty(STDIN_FILENO))
        return cmd_read_password_line(prompt, buf, buf_size);

    fputs("\n", stderr);
    fputs(prompt, stderr);
    fflush(stderr);

    if (tcgetattr(STDIN_FILENO, &old_tty) != 0)
        return -1;
    new_tty = old_tty;
    new_tty.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_tty) != 0)
        return -1;

    if (cmd_read_password_line("", buf, buf_size) == 0)
        rc = 0;
    fputc('\n', stderr);
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tty);
    return rc;
}
#endif

int cmd_read_password(const char *prompt, char *buf, size_t buf_size) {
    const char *env_pw;
    const char *line_prompt = "Password: ";

    if (!buf || buf_size < 2)
        return -1;

    /* FL_CMD_PASSWORD: non-interactive lab/CI only; not for production auth bypass. */
    env_pw = getenv("FL_CMD_PASSWORD");
    if (env_pw && env_pw[0]) {
        strncpy(buf, env_pw, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return 0;
    }

    if (!prompt || !prompt[0])
        prompt = line_prompt;

#if defined(__unix__) || defined(__APPLE__)
    return cmd_read_password_tty(prompt, buf, buf_size);
#else
    fputs("\n", stderr);
    fputs(prompt, stderr);
    fflush(stderr);
    return cmd_read_password_line(prompt, buf, buf_size);
#endif
}

int cmd_sudo_require_auth_reason(const char *reason) {
    char pw[128];
    fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
    fl_result_t rc;
    const char *why = (reason && reason[0]) ? reason : "sudo authentication";

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

    rc = fl_session_grant_elevation(why, &tok);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo: elevation failed (%d)\n", (int)rc);
        return 1;
    }
    fl_audit_elevation_event(fl_session_current_user(), why, 1);
    return 0;
}

int cmd_sudo_require_auth(void) {
    return cmd_sudo_require_auth_reason("sudo authentication");
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

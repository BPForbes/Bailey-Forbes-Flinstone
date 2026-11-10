#include "cmd_authutil.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "fl/kq.h"
#include "heritage.h"
#include "fl/mem_asm.h"
#include "mem_domain.h"
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
    /* The secret-wipe primitive: a plain memset of a dying buffer may be optimised away. */
    mem_domain_secure_zero(buf, buf_size);
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

/*
 * One line that is not a secret (an account name), read by the reader above. The rest of a line
 * longer than buf is read and discarded, so it never reaches the next reader (the shell would run it
 * as a command), and *overlong says so. A full buffer with no newline removed is the only case where
 * more of the line can remain.
 */
static int cmd_read_line(char *buf, size_t buf_size, int *overlong) {
    *overlong = 0;
    if (cmd_read_password_line(NULL, buf, buf_size) != 0)
        return -1;
    if (buf_size > 1 && buf_size <= (size_t)INT_MAX && strlen(buf) + 1 == buf_size) {
        int c = getc(stdin);

        if (c != '\n' && c != EOF) {
            *overlong = 1;
            while ((c = getc(stdin)) != '\n' && c != EOF)
                ;
        }
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

    /* KeyQuorum's sudo factor is Root's key: Root's password and token for a KQST-mode Root; the
     * caller's own password for a password-only Root, as before (fl_session_sudo_authenticate). */
    if (cmd_read_password(fl_session_sudo_needs_root() ? "[sudo] Root password: " : "[sudo] password: ",
                          pw, sizeof(pw)) != 0) {
        cmd_wipe_password(pw, sizeof(pw));
        fprintf(stderr, "sudo: password read failed\n");
        return 1;
    }
    if (fl_session_sudo_authenticate(pw) != FL_RESULT_OK) {
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

/* 1 when key_id is a Member of the live bridge uid in the account store. */
static int bridge_has_member(const char *uid, const char *key_id) {
    fl_user_bridge_t *b = calloc(1, sizeof *b);
    size_t i;
    int found = 0;

    if (!b)
        return 0;
    if (fl_user_db_bridge_get(fl_session_user_db(), uid, b) == FL_RESULT_OK)
        for (i = 0; i < b->nmembers && !found; i++)
            found = strcmp(b->members[i].label, key_id) == 0;
    free(b);
    return found;
}

int cmd_switch_user(const char *cmd, const char *target, int sudo) {
    char pw[128], member[FL_USER_NAME_MAX], member_pw[128];
    char prompt[160];
    fl_session_switch_request_t req;
    fl_kq_requirement_t need;
    const fl_user_db_t *db;
    const fl_user_record_t *root;
    fl_result_t rc;
    int ask_password = 1;

    fl_session_init();
    db = fl_session_user_db();
    if (!target || !target[0] || !fl_user_db_find(db, target)) {
        fprintf(stderr, "%s: user %s does not exist\n", cmd, target ? target : "");
        return 1;
    }
    memset(&req, 0, sizeof req);
    req.target = target;
    req.sudo = sudo;
    pw[0] = member[0] = member_pw[0] = '\0';
    /* Which factor to collect is KeyQuorum's answer; the session asks again and decides. */
    need = fl_kq_unlock_requirement(fl_session_current_user(), target, sudo);
    root = fl_user_db_find_by_key(db, FL_HERITAGE_ROOT_KEY);
    if (need.factor == FL_KQ_ACCESS_NONE)
        ask_password = 0;
    /* A password-only Root keeps the old sudo -i: no prompt for an elevated account. */
    if (sudo && fl_session_is_elevated_account() && root && root->auth_mode != FL_USER_AUTH_PASSWORD_KQST)
        ask_password = 0;
    /* A refused switch asks for the password too: whether it is refused, and why, is not shown first. */
    if (ask_password) {
        if (sudo)
            snprintf(prompt, sizeof prompt, "[sudo -i] Root password: ");
        else
            snprintf(prompt, sizeof prompt, "Password: ");
        if (cmd_read_password(prompt, pw, sizeof pw) != 0) {
            cmd_wipe_password(pw, sizeof pw);
            fprintf(stderr, "%s: password read failed\n", cmd);
            return 1;
        }
        req.password = pw;
    }
    /*
     * A bridge member is asked for only once the password is verified (the target's, or Root's for
     * sudo -i), so the prompt says nothing about a bridge to someone without it. Otherwise the session
     * refuses on the password and audits it.
     */
    if (need.factor == FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST && !need.needs_parent_grant && req.password &&
        fl_session_verify_account_password(sudo ? (root ? root->name : "") : target, pw)) {
        /* A member of the bridge presents their slot token: the caller, when a member, else named. */
        const char *self = fl_session_current_user();
        const fl_user_record_t *me = fl_user_db_find(db, self);
        int overlong = 0;

        if (me && bridge_has_member(need.bridge_uid, me->key_id)) {
            snprintf(member, sizeof member, "%s", me->name);
        } else {
            fputs("Bridge member account: ", stderr);
            fflush(stderr);
            if (cmd_read_line(member, sizeof member, &overlong) != 0)
                member[0] = '\0';
            member[strcspn(member, "\r\n")] = '\0';
        }
        if (overlong || !member[0]) {
            /* An over-long or empty name names nobody: the session refuses ("bridge-member"). */
            member[0] = '\0';
        } else {
            snprintf(prompt, sizeof prompt, "Password for %s (bridge slot token): ", member);
            if (cmd_read_password(prompt, member_pw, sizeof member_pw) != 0) {
                cmd_wipe_password(pw, sizeof pw);
                cmd_wipe_password(member_pw, sizeof member_pw);
                fprintf(stderr, "%s: password read failed\n", cmd);
                return 1;
            }
            req.bridge_member = member;
            req.bridge_member_password = member_pw;
        }
    }
    rc = fl_session_switch_user(&req);
    cmd_wipe_password(pw, sizeof pw);
    cmd_wipe_password(member_pw, sizeof member_pw);
    if (rc != FL_RESULT_OK) {
        /* One message for every refusal; the reason is in the audit only. */
        fprintf(stderr, "%s: authentication failed\n", cmd);
        return 1;
    }
    return 0;
}

fl_result_t cmd_open_account_keys(const char *why, unsigned char *signing, unsigned char *box) {
    char pw[128];
    char prompt[96];
    const char *name = fl_session_current_user();
    fl_user_record_t rec;
    int created = 0;
    fl_result_t rc;

    snprintf(prompt, sizeof prompt, "Password (%s): ", why);
    if (cmd_read_password(prompt, pw, sizeof(pw)) != 0) {
        cmd_wipe_password(pw, sizeof(pw));
        return FL_RESULT_EOF;
    }
    rc = fl_session_ensure_key(name, pw, &created);
    if (rc == FL_RESULT_OK && created && fl_session_save_users() != FL_RESULT_OK) {
        /* A key the store does not hold must not sign anything: drop what was just made. */
        fl_session_forget_created(name, created);
        rc = FL_RESULT_ERR;
    }
    if (rc == FL_RESULT_OK)
        rc = fl_session_snapshot_account(name, &rec);
    if (rc == FL_RESULT_OK && signing)
        rc = fl_user_db_open_key_record(&rec, pw, signing);
    if (rc == FL_RESULT_OK && box)
        rc = fl_user_db_open_box_record(&rec, pw, box);
    cmd_wipe_password(pw, sizeof(pw));
    return rc;
}

#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_authutil.h"
#include "common.h"
#include "fl/audit_log.h"
#include "fl/authz_subsystem.h"
#include "fl/session.h"
#include "user_db.h"
#include "session_sync.h"
#include "session_login_env.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct {
    const char *target;
    const char *command;
    const char *password;
    int         login_shell;
} su_request_t;

static int su_parse_args(int argc, char **argv, su_request_t *req) {
    int i = 1;

    memset(req, 0, sizeof(*req));
    req->target = "root";

    while (i < argc) {
        if (!strcmp(argv[i], "-")) {
            req->login_shell = 1;
            i++;
            continue;
        }
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            req->command = argv[i + 1];
            i += 2;
            continue;
        }
        if (argv[i][0] == '-')
            return -1;
        req->target = argv[i];
        i++;
        break;
    }
    return 0;
}

static int su_run_command(const char *command) {
    pid_t pid;
    int status = 0;
    int rc = 1;

    pid = fork();
    if (pid < 0) {
        perror("su");
        return 1;
    }
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror("su");
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        perror("su");
        return 1;
    }
    if (WIFEXITED(status))
        rc = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        rc = 128 + WTERMSIG(status);
    return rc;
}

int cmd_su_run(int argc, char **argv) {
    su_request_t req;
    char saved[FL_USER_NAME_MAX];
    char switched[FL_USER_NAME_MAX];
    char saved_home[CWD_MAX];
    char saved_cwd[CWD_MAX];
    uint64_t login_gen;
    int had_elevation;
    fl_result_t rc;

    if (argc < 1) {
        fprintf(stderr, "su: usage: su [-] [username]\n");
        fprintf(stderr, "su:        su -c 'command' [username]\n");
        return 1;
    }
    if (su_parse_args(argc, argv, &req) != 0) {
        fprintf(stderr, "su: invalid options\n");
        return 1;
    }
    if (fl_authz_identity_switch_denied("su"))
        return 1;

    strncpy(saved, fl_session_current_user(), sizeof(saved) - 1);
    saved[sizeof(saved) - 1] = '\0';
    had_elevation = fl_session_has_elevation();
    /* The restore below happens only while this login is still the current one. */
    login_gen = fl_session_login_generation();

    /* KeyQuorum's factor, collected here and checked and committed by the session. */
    if (cmd_switch_user("su", req.target, 0) != 0)
        return 1;
    /* The switch ended the elevation (the session revokes it at commit): audited only now. */
    if (had_elevation)
        fl_audit_elevation_event(saved, "su user switch", 0);
    fl_session_sync_services();
    if (req.login_shell) {
        fl_session_save_login_shell_env(saved_home, sizeof(saved_home),
                                        saved_cwd, sizeof(saved_cwd));
        if (fl_session_apply_login_shell_env(req.target) != 0) {
            fprintf(stderr, "su: failed to apply login-shell environment for %s\n",
                    req.target);
            fl_session_restore_login_shell_env(saved_home, saved_cwd);
            if (fl_session_restore_user(saved, login_gen) != FL_RESULT_OK)
                fprintf(stderr, "su: session user not restored (a login or logout ran meanwhile)\n");
            fl_session_sync_services();
            return 1;
        }
    }

    if (req.command) {
        int cmd_rc = su_run_command(req.command);

        strncpy(switched, fl_session_current_user(), sizeof(switched) - 1);
        switched[sizeof(switched) - 1] = '\0';
        had_elevation = fl_session_has_elevation();
        rc = fl_session_restore_user(saved, login_gen);
        if (rc == FL_RESULT_BUSY) {
            /* Another login took the session meanwhile: it is left as it is (audited by the session). */
            fprintf(stderr, "su: session user not restored (a login or logout ran meanwhile)\n");
            fl_session_sync_services();
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "su: cannot restore session user %s (%d)\n", saved, (int)rc);
            if (fl_session_restore_user("guest", login_gen) == FL_RESULT_OK && had_elevation)
                fl_audit_elevation_event(switched, "su restore", 0);
            if (req.login_shell)
                fl_session_restore_login_shell_env(saved_home, saved_cwd);
            fl_session_sync_services();
            return 1;
        }
        if (had_elevation)
            fl_audit_elevation_event(switched, "su restore", 0);
        if (req.login_shell)
            fl_session_restore_login_shell_env(saved_home, saved_cwd);
        fl_session_sync_services();
        return cmd_rc;
    }

    printf("su: switched to %s%s\n", req.target,
           req.login_shell ? " (login shell)" : "");
    return 0;
}

int cmd_su_batch_tokens_count(int argc, char **argv, int i) {
    int j;
    int saw_user = 0;

    if (i >= argc || !argv[i] || strcmp(argv[i], "su"))
        return 1;
    j = i + 1;
    while (j < argc && argv[j]) {
        if (!strcmp(argv[j], "-c")) {
            if (saw_user)
                break;
            if (j + 1 >= argc)
                return (j + 1) - i;
            if (j + 2 < argc && argv[j + 2] && argv[j + 2][0] != '-')
                return (j + 3) - i;
            return (j + 2) - i;
        }
        if (argv[j][0] == '-' && strcmp(argv[j], "-") && strcmp(argv[j], "-c"))
            break;
        if (argv[j][0] != '-') {
            if (saw_user)
                break;
            saw_user = 1;
        }
        j++;
    }
    if (j <= i)
        return 1;
    return j - i;
}

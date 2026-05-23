#include "cmd_decl.h"
#include "cmd_authutil.h"
#include "common.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "session_sync.h"
#include "session_login_env.h"
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
    char saved[32];
    char saved_home[CWD_MAX];
    char saved_cwd[CWD_MAX];
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

    if (cmd_su_require_auth(req.target) != 0)
        return 1;

    strncpy(saved, fl_session_current_user(), sizeof(saved) - 1);
    saved[sizeof(saved) - 1] = '\0';

    if (fl_session_has_elevation())
        fl_audit_elevation_event(saved, "su user switch", 0);

    rc = fl_session_set_user(req.target);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "su: cannot switch to %s (%d)\n", req.target, (int)rc);
        return 1;
    }
    fl_session_sync_services();
    if (req.login_shell) {
        fl_session_save_login_shell_env(saved_home, sizeof(saved_home),
                                        saved_cwd, sizeof(saved_cwd));
        fl_session_apply_login_shell_env(req.target);
    }

    if (req.command) {
        int cmd_rc = su_run_command(req.command);
        if (fl_session_has_elevation())
            fl_audit_elevation_event(fl_session_current_user(), "su restore", 0);
        rc = fl_session_set_user(saved);
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "su: cannot restore session user %s (%d)\n", saved, (int)rc);
            return 1;
        }
        if (req.login_shell)
            fl_session_restore_login_shell_env(saved_home, saved_cwd);
        fl_session_sync_services();
        return cmd_rc;
    }

    printf("su: switched to %s%s\n", req.target,
           req.login_shell ? " (login shell)" : "");
    return 0;
}

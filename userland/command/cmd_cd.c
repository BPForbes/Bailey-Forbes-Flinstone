#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "mem_domain.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int cmd_cd_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("%s\n", g_cwd);
        return 0;
    }
    char resolved[CWD_MAX];
    mem_domain_zero(resolved, sizeof(resolved));
    resolve_path(args[1], resolved, sizeof(resolved));
    if (cmd_jail_blocked_path("cd", args[1], resolved))
        return 1;
    char oldcwd[CWD_MAX];
    mem_domain_zero(oldcwd, sizeof(oldcwd));
    if (getcwd(oldcwd, sizeof(oldcwd)) == NULL) {
        perror("cd: getcwd");
        return 1;
    }
    if (chdir(resolved) == 0) {
        if (getcwd(g_cwd, sizeof(g_cwd)) == NULL) {
            strncpy(g_cwd, resolved, sizeof(g_cwd) - 1);
            g_cwd[sizeof(g_cwd) - 1] = '\0';
        }
        if (cmd_jail_blocked_path("cd", args[1], g_cwd)) {
            if (chdir(oldcwd) == 0) {
                strncpy(g_cwd, oldcwd, sizeof(g_cwd) - 1);
                g_cwd[sizeof(g_cwd) - 1] = '\0';
            }
            return 1;
        }
        path_log_record(PATH_OP_CD, g_cwd);
        printf("%s\n", g_cwd);
    } else {
        perror("cd");
        return 1;
    }
    return 0;
}

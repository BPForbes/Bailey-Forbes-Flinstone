#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "mem_domain.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * Change the current working directory to the resolved target or print the current directory when no target is provided.
 *
 * Resolves argv[1] into a canonical path, enforces jail/block checks before and after changing directories, updates the global
 * `g_cwd` on success, records the operation via path_log_record, and prints the resulting working directory. If the post-change
 * jail check fails the function attempts to revert to the previous directory and restore `g_cwd`. Errors are reported with `perror`.
 *
 * @param argc Number of command-line arguments; when less than 2 the current `g_cwd` is printed.
 * @param argv Argument vector where `argv[1]` is the desired target path.
 * @returns `0` on success (including the no-argument print case), `1` on any error or when a jail/block check prevents the change.
 */
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
        if (getcwd(g_cwd, sizeof(g_cwd)) == NULL)
            strncpy(g_cwd, resolved, sizeof(g_cwd) - 1);
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

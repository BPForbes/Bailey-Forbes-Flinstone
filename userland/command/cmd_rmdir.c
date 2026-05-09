#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "util.h"
#include <stdio.h>
#include <unistd.h>

/**
 * Remove a directory specified by the command arguments.
 *
 * Resolves the target path and enforces jail/path-block restrictions. Attempts
 * removal through the global file-manager service when available, otherwise
 * falls back to the system rmdir call. On successful removal the operation is
 * recorded and a confirmation is printed; on failure an error is reported via
 * perror().
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments; argv[1] is the directory to remove.
 * @returns `0` after attempting the removal, `1` if the argument is missing or the path is blocked by the jail.
 */
int cmd_rmdir_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: rmdir <directory>\n");
        return 1;
    }
    char rpath[CWD_MAX];
    resolve_path(args[1], rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("rmdir", args[1], rpath))
        return 1;
    if (g_fm_service) {
        if (fm_delete(g_fm_service, rpath) == 0) {
            path_log_record(PATH_OP_DELETE, rpath);
            printf("Directory '%s' removed.\n", rpath);
        } else
            perror("rmdir");
    } else if (rmdir(rpath) == 0) {
        path_log_record(PATH_OP_DELETE, rpath);
        printf("Directory '%s' removed.\n", rpath);
    } else {
        perror("rmdir");
    }
    return 0;
}

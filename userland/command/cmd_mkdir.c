#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "disk.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

/**
 * Execute the `mkdir` command using argv[1] as the target directory.
 *
 * Resolves the provided path, enforces jail restrictions, attempts to create the
 * directory (via file-manager service if available or local helper otherwise),
 * and records the operation in the path log. Prints usage or result messages
 * and may call `perror("mkdir")` on service creation failure.
 *
 * @param argc Number of command-line arguments; must be >= 2 (program name + target).
 * @param argv Argument vector where argv[1] is the directory path to create.
 * @returns 0 on completion after attempting creation; 1 if usage is incorrect or the path is blocked.
 */
int cmd_mkdir_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: mkdir <directory>\n");
        return 1;
    }
    char rpath[CWD_MAX];
    resolve_path(args[1], rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("mkdir", args[1], rpath))
        return 1;
    if (g_fm_service) {
        if (fm_create_dir(g_fm_service, rpath) == 0) {
            path_log_record(PATH_OP_CREATE, rpath);
            printf("Directory '%s' created.\n", rpath);
        } else
            perror("mkdir");
    } else {
        create_directory(rpath);
        path_log_record(PATH_OP_CREATE, rpath);
    }
    return 0;
}

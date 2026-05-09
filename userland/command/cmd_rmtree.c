#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>

/**
 * Execute the `rmtree` command to recursively delete a specified directory.
 *
 * @param argc Number of command-line arguments; must be >= 2.
 * @param argv Argument vector where `argv[1]` is the path to the directory to remove.
 * @returns `0` on success (removal initiated), `1` on failure (insufficient arguments or path blocked by jail policy).
 */
int cmd_rmtree_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: rmtree <directory>\n");
        return 1;
    }
    char rpath[CWD_MAX];
    resolve_path(args[1], rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("rmtree", args[1], rpath))
        return 1;
    path_log_record(PATH_OP_DELETE, rpath);
    remove_directory_recursive(rpath);
    return 0;
}

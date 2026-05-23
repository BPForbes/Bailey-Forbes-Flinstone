#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>

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

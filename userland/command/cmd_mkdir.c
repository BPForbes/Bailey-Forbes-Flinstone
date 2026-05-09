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

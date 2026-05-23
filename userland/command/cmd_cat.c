#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "fs.h"
#include "disk.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

int cmd_cat_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: cat <filename>\n");
        return 1;
    }
    char rpath[CWD_MAX];
    resolve_path(args[1], rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("cat", args[1], rpath))
        return 1;
    if (g_fm_service) {
        char buf[4096];
        if (fm_read_text(g_fm_service, rpath, buf, sizeof(buf)) >= 0) {
            path_log_record(PATH_OP_READ, rpath);
            printf("%s", buf);
        } else
            perror("cat");
    } else {
        path_log_record(PATH_OP_READ, rpath);
        cat_file(rpath);
    }
    return 0;
}

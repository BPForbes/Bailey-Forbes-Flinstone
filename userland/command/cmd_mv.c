#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "util.h"
#include <stdio.h>

int cmd_mv_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 3) {
        printf("Usage: mv <src> <dst>\n");
        return 1;
    }
    char srcpath[CWD_MAX], dstpath[CWD_MAX];
    resolve_path(args[1], srcpath, sizeof(srcpath));
    resolve_path(args[2], dstpath, sizeof(dstpath));
    if (cmd_jail_blocked_path("mv", args[1], srcpath) ||
        cmd_jail_blocked_path("mv", args[2], dstpath))
        return 1;
    if (g_fm_service) {
        if (fm_move(g_fm_service, srcpath, dstpath) == 0) {
            path_log_record(PATH_OP_MOVE, srcpath);
            path_log_record(PATH_OP_MOVE, dstpath);
            printf("Moved '%s' to '%s'\n", srcpath, dstpath);
        } else {
            perror("mv");
            return 1;
        }
    } else {
        if (rename(srcpath, dstpath) == 0) {
            path_log_record(PATH_OP_MOVE, srcpath);
            path_log_record(PATH_OP_MOVE, dstpath);
            printf("Moved '%s' to '%s'\n", srcpath, dstpath);
        } else {
            perror("mv");
            return 1;
        }
    }
    return 0;
}

int cmd_mv_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 3;
}

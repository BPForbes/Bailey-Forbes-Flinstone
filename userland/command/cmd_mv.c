#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "util.h"
#include <stdio.h>

/**
 * Move or rename a source path to a destination path.
 *
 * Resolves the provided source and destination arguments to absolute paths,
 * enforces jail/path restrictions, then performs the move operation. If a
 * filesystem manager service is available the service is used; otherwise the
 * operation falls back to the standard rename syscall. Successful moves are
 * recorded in the path log and a confirmation message is printed; failures
 * emit an error via perror.
 *
 * @param argc Number of command-line arguments (must be >= 3).
 * @param argv Argument vector where argv[1] is the source path and argv[2] is
 *             the destination path.
 * @returns `0` after attempting the move operation; `1` if argument validation
 *          fails or either path is blocked by jail restrictions.
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
        } else
            perror("mv");
    } else {
        if (rename(srcpath, dstpath) == 0) {
            path_log_record(PATH_OP_MOVE, srcpath);
            path_log_record(PATH_OP_MOVE, dstpath);
            printf("Moved '%s' to '%s'\n", srcpath, dstpath);
        } else
            perror("mv");
    }
    return 0;
}

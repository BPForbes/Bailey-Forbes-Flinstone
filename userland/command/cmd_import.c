#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "fs.h"
#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int cmd_import_run(int argc, char **argv) {
    char **args = argv;
    if (argc == 3) {
        char srcpath[CWD_MAX], dstpath[CWD_MAX];
        resolve_path(args[1], srcpath, sizeof(srcpath));
        resolve_path(args[2], dstpath, sizeof(dstpath));
        if (cmd_jail_blocked_path("import", args[1], srcpath) ||
            cmd_jail_blocked_path("import", args[2], dstpath))
            return 1;
        import_text_drive(srcpath, dstpath, -1, -1);
        return 0;
    } else if (argc == 5) {
        char *end_count = NULL;
        char *end_size = NULL;
        errno = 0;
        long count_l = strtol(args[3], &end_count, 10);
        long size_l = strtol(args[4], &end_size, 10);
        if (*args[3] == '\0' || *args[4] == '\0' ||
            *end_count != '\0' || *end_size != '\0' || errno == ERANGE ||
            count_l <= 0 || count_l > 65535 || size_l <= 0 || size_l > 65535) {
            printf("Invalid geometry for import.\n");
            return 1;
        }
        int count = (int)count_l;
        int size = (int)size_l;
        char srcpath[CWD_MAX], dstpath[CWD_MAX];
        resolve_path(args[1], srcpath, sizeof(srcpath));
        resolve_path(args[2], dstpath, sizeof(dstpath));
        if (cmd_jail_blocked_path("import", args[1], srcpath) ||
            cmd_jail_blocked_path("import", args[2], dstpath))
            return 1;
        import_text_drive(srcpath, dstpath, count, size);
        return 0;
    }
    printf("Usage: import <listfile> <diskfile> [clusters clusterSize]\n");
    return 1;
}

int cmd_import_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 4 < argc) {
        char *end_count = NULL;
        char *end_size = NULL;
        long count_l;
        long size_l;

        errno = 0;
        count_l = strtol(argv[i + 3], &end_count, 10);
        int count_ok = (argv[i + 3][0] != '\0' && *end_count == '\0' &&
                        errno != ERANGE && count_l > 0 && count_l <= 65535);

        errno = 0;
        size_l = strtol(argv[i + 4], &end_size, 10);
        int size_ok = (argv[i + 4][0] != '\0' && *end_size == '\0' &&
                       errno != ERANGE && size_l > 0 && size_l <= 65535);

        if (count_ok && size_ok)
            return 5;
    }
    return 3;
}

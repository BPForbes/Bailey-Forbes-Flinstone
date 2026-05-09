#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "fs.h"
#include "util.h"
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
        int count = atoi(args[3]);
        int size = atoi(args[4]);
        if (count <= 0 || count > 65535 || size <= 0 || size > 65535) {
            printf("Invalid geometry for import.\n");
            return 1;
        }
        char srcpath[CWD_MAX], dstpath[CWD_MAX];
        resolve_path(args[1], srcpath, sizeof(srcpath));
        resolve_path(args[2], dstpath, sizeof(dstpath));
        if (cmd_jail_blocked_path("import", args[1], srcpath) ||
            cmd_jail_blocked_path("import", args[2], dstpath))
            return 1;
        import_text_drive(srcpath, dstpath, count, size);
        return 0;
    }
    printf("Usage: import <textfile> <txtfile> [clusters clusterSize]\n");
    return 1;
}

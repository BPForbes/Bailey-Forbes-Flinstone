#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Handle the `import` command: parse arguments, enforce jail restrictions, and dispatch an import.
 *
 * Accepts either `import <textfile> <txtfile>` or
 * `import <textfile> <txtfile> <clusters> <clusterSize>`. Resolves and normalizes source and
 * destination paths, checks them with the jail blocker, and calls `import_text_drive` with either
 * inferred geometry (-1, -1) or the provided cluster geometry.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector where argv[1] is the source text file and argv[2] is the destination.
 * @returns `0` on successful dispatch of the import operation, `1` on invalid usage, invalid geometry,
 *          or when a path is blocked by the jail checks.
 */
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

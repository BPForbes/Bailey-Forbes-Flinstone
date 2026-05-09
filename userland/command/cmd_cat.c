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

/**
 * Print the contents of a file to standard output as the `cat` command.
 *
 * Attempts to resolve and validate the provided filename, records the read
 * operation, and writes the file's text contents to stdout using the
 * configured file-access backend or a local fallback.
 *
 * @param argc Number of command-line arguments; must be at least 2.
 * @param argv Argument vector where argv[1] is the target filename to display.
 * @returns 0 on success, 1 on usage error or if the resolved path is blocked. 
 */
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

#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

/**
 * Write provided text arguments to a resolved file path.
 *
 * Concatenates argv[2]..argv[argc-1] into a single space-separated string (truncated to 4096 bytes),
 * resolves and checks the target path against the jail, then writes the content to that path
 * (using the filesystem manager service if available, otherwise stdio). On successful write records
 * a PATH_OP_WRITE log entry and prints a confirmation message; on write failure prints an error via perror.
 *
 * @param argc Number of arguments; must be at least 3 (command, filename, content).
 * @param argv Argument vector where argv[1] is the target filename and argv[2..] form the content.
 * @returns 0 on completion after attempting the write, 1 if usage is invalid or the path is blocked by the jail.
 */
int cmd_write_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 3) {
        printf("Usage: write <filename> <content>\n");
        return 1;
    }
    char rpath[CWD_MAX];
    resolve_path(args[1], rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("write", args[1], rpath))
        return 1;
    char content[4096] = {0};
    for (int i = 2; i < argc && (size_t)(strlen(content) + strlen(args[i]) + 2) < sizeof(content); i++) {
        if (i > 2)
            strcat(content, " ");
        strcat(content, args[i]);
    }
    if (g_fm_service) {
        if (fm_save_text(g_fm_service, rpath, content) == 0) {
            path_log_record(PATH_OP_WRITE, rpath);
            printf("Wrote to '%s'\n", rpath);
        } else
            perror("write");
    } else {
        FILE *f = fopen(rpath, "w");
        if (f) {
            fputs(content, f);
            fclose(f);
            path_log_record(PATH_OP_WRITE, rpath);
            printf("Wrote to '%s'\n", rpath);
        } else
            perror("write");
    }
    return 0;
}

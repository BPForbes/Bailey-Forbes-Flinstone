#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

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

int cmd_write_batch_tokens_count(int argc, char **argv, int i) {
    (void)argv;
    return (argc > i + 2) ? (argc - i) : 0;
}

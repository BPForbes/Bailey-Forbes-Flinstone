#include "common.h"
#include "cmd_decl.h"
#include "fs.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "cmd_util.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_make_maybe(char *trimmed) {
    if (strncmp(trimmed, "make ", 5) != 0)
        return 0;
    char *filename = trimmed + 5;
    while (*filename == ' ' || *filename == '\t')
        filename++;
    if (!*filename)
        return 0;
    char rpath[CWD_MAX];
    resolve_path(filename, rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("make", filename, rpath))
        return 1;
    if (g_fm_service) {
        fm_create_file(g_fm_service, rpath);
        path_log_record(PATH_OP_CREATE, rpath);
        printf("Creating file '%s'. Enter lines (end with 'EOF'):\n", rpath);
        char content[4096] = {0};
        size_t off = 0;
        char buf[256];
        while (1) {
            printf("file> ");
            fflush(stdout);
            if (!fgets(buf, sizeof(buf), stdin))
                break;
            buf[strcspn(buf, "\n")] = '\0';
            if (!strcmp(buf, "EOF")) {
                printf("Done writing '%s'.\n", rpath);
                break;
            }
            if (off + strlen(buf) + 2 < sizeof(content))
                off += (size_t)snprintf(content + off, sizeof(content) - off, "%s\n", buf);
        }
        fm_save_text(g_fm_service, rpath, content);
    } else {
        do_make_file(rpath);
    }
    return 1;
}

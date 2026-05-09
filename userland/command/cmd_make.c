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

/**
 * Handle a "make <filename>" command by creating the named file and, when possible,
 * interactively collecting file contents from stdin until a line containing `EOF`.
 *
 * Examines `trimmed` for the "make " prefix; if present and a filename is provided,
 * resolves the target path, rejects the operation if it is disallowed by the jail check,
 * and then attempts to create the file. If a file-manager service is available the
 * function prompts for lines to write into the new file; otherwise it performs a
 * non-interactive create. If the input does not start with "make " or no filename is
 * given, the call is not handled.
 *
 * @param trimmed Command string to inspect; expected to begin with "make " followed by the filename.
 * @returns `1` if the input was handled (file creation attempted or blocked by jail), `0` if the input did not match the "make" command or the filename was empty.
 */
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

#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "fs_types.h"
#include "disk.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>

/**
 * Execute the "dir" command to list files in a target directory.
 *
 * Resolves the target path (uses argv[1] if provided and does not start with '-',
 * otherwise uses "."), enforces jail restrictions, and lists the directory contents.
 * When a file-manager service is available, the service is used; otherwise a local
 * listing routine is invoked.
 *
 * @param argc Number of command arguments; argv[1] may be an optional target path.
 * @param argv Argument vector for the command.
 * @returns `0` on success, `1` if the target path is blocked by jail restrictions.
 */
int cmd_dir_run(int argc, char **argv) {
    char **args = argv;
    char rpath[CWD_MAX];
    resolve_path((argc >= 2 && args[1][0] != '-') ? args[1] : ".", rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("dir", (argc >= 2 && args[1][0] != '-') ? args[1] : ".", rpath))
        return 1;
    if (g_fm_service) {
        fs_node_t *nodes;
        int count;
        if (fm_list(g_fm_service, rpath, &nodes, &count) == 0) {
            path_log_record(PATH_OP_DIR, rpath);
            printf("Files in '%s':\n", rpath);
            for (int i = 0; i < count; i++)
                printf("  %s\n", nodes[i].name);
            fs_nodes_free(nodes, count);
        } else {
            printf("Cannot open '%s'\n", rpath);
        }
    } else {
        list_files(rpath);
    }
    return 0;
}

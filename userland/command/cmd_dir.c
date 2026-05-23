#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "fs_types.h"
#include "disk.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>

int cmd_dir_run(int argc, char **argv) {
    char **args = argv;
    const char *target = (argc >= 2 && args[1][0] != '-') ? args[1] : ".";
    char rpath[CWD_MAX];
    resolve_path(target, rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("dir", target, rpath))
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

int cmd_dir_batch_tokens_count(int argc, char **argv, int i) {
    return cmd_batch_opt_path_arity(argc, argv, i);
}

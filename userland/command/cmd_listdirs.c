#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "path_log.h"
#include "fs_service_glue.h"
#include "fs_types.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>

int cmd_listdirs_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char rpath[CWD_MAX];
    resolve_path(".", rpath, sizeof(rpath));
    if (cmd_jail_blocked_path("listdirs", ".", rpath))
        return 1;
    if (g_fm_service) {
        fs_node_t *nodes;
        int count;
        if (fm_list(g_fm_service, rpath, &nodes, &count) == 0) {
            path_log_record(PATH_OP_DIR, rpath);
            printf("Directories in current path:\n");
            for (int i = 0; i < count; i++)
                if (nodes[i].type == NODE_DIR)
                    printf("  %s\n", nodes[i].name);
            fs_nodes_free(nodes, count);
        }
    } else {
        path_log_record(PATH_OP_DIR, rpath);
        list_directories();
    }
    return 0;
}

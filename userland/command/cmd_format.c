#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "disk.h"
#include "mem_domain.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_format_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 5) {
        printf("Usage: format <disk_file> <volume> <rowCount> <nibbleCount>\n");
        return 1;
    }
    int rowCount = atoi(args[3]);
    int nibbleCount = atoi(args[4]);
    if (rowCount <= 0 || nibbleCount <= 0 || (nibbleCount % 2 != 0)) {
        printf("Error: rowCount must be positive and nibbleCount must be positive and even.\n");
        return 1;
    }
    char fpath[CWD_MAX];
    mem_domain_zero(fpath, sizeof(fpath));
    resolve_path(args[1], fpath, sizeof fpath);
    if (cmd_jail_blocked_path("format", args[1], fpath))
        return 1;
    format_disk_file(fpath, args[2], rowCount, nibbleCount);
    strncpy(current_disk_file, fpath, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    g_cluster_size = nibbleCount / 2;
    g_total_clusters = rowCount;
    return 0;
}

int cmd_format_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 5;
}

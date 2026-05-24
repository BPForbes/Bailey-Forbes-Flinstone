#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include <stdio.h>
#include <stdlib.h>

int cmd_initdisk_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 3) {
        printf("Usage: initdisk <cluster_count> <cluster_size>\n");
        return 1;
    }
    int newCount = atoi(args[1]);
    int newSize = atoi(args[2]);
    if (newCount <= 0 || newSize <= 0 || newCount > 65535 || newSize > 65535) {
        printf("Invalid geometry.\n");
        return 1;
    }
    g_total_clusters = newCount;
    g_cluster_size = newSize;
    printf("Soft init: clusters=%d, size=%d bytes (in memory only).\n", g_total_clusters, g_cluster_size);
    return 0;
}

int cmd_initdisk_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 3;
}

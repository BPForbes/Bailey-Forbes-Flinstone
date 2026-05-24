#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cluster.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_update_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 4) {
        printf("Usage: update <cluster_index> -t|-h <data>\n");
        return 1;
    }
    int clu = atoi(args[1]);
    if (clu < 0 || clu >= g_total_clusters) {
        printf("Cluster out of range [0..%d].\n", g_total_clusters - 1);
        return 1;
    }
    delete_cluster(clu);
    int inputIsText = (!strcmp(args[2], "-t")) ? 1 : 0;
    if (process_write_cluster(clu, args[3], inputIsText) != 0) {
        printf("Failed to write cluster %d.\n", clu);
        return 1;
    }
    printf("Cluster %d updated.\n", clu);
    if (calculate_storage_breakdown_for_cluster(clu) != 0) {
        printf("Failed to compute storage breakdown for cluster %d.\n", clu);
        return 1;
    }
    return 0;
}

int cmd_update_batch_tokens_count(int argc, char **argv, int i) {
    int remaining;

    (void)argv;
    if (i < 0 || i >= argc)
        return 0;
    remaining = argc - i;
    if (remaining < 4)
        return remaining;
    return 4;
}

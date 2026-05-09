#include "common.h"
#include "cmd_decl.h"
#include "cluster.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Update an existing cluster by replacing its contents and recomputing its storage breakdown.
 *
 * Expects arguments: argv[1] = cluster_index, argv[2] = "-t" for text or "-h" for hex, argv[3] = data to write.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector (see expected layout above).
 * @returns `0` on success, `1` on invalid usage or when the specified cluster index is out of range.
 */
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
    process_write_cluster(clu, args[3], inputIsText);
    printf("Cluster %d updated.\n", clu);
    calculate_storage_breakdown_for_cluster(clu);
    return 0;
}

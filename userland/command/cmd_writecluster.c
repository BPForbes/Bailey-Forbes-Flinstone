#include "common.h"
#include "cmd_decl.h"
#include "cluster.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Handle the `writecluster` command by parsing arguments, writing data to the specified cluster,
 * and updating the cluster's storage breakdown.
 *
 * Expects argv to contain: argv[1] = cluster index, argv[2] = "-t" for text input or "-h" for hex,
 * and argv[3] = data to write.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector containing the command and its parameters.
 * @returns `0` on success, `1` if insufficient arguments (prints usage message).
 */
int cmd_writecluster_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 4) {
        printf("Usage: writecluster <cluster_index> -t|-h <data>\n");
        return 1;
    }
    int clu = atoi(args[1]);
    int inputIsText = (!strcmp(args[2], "-t")) ? 1 : 0;
    process_write_cluster(clu, args[3], inputIsText);
    calculate_storage_breakdown_for_cluster(clu);
    return 0;
}
